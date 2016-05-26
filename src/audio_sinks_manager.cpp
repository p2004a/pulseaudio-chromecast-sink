#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <boost/asio/io_service.hpp>

#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/stream.h>
#include <pulse/subscribe.h>

#include "audio_sinks_manager.h"
#include "util.h"

struct ContextOperation {
    ContextOperation(AudioSinksManager* manager_, std::string name_, bool report_on_fail_ = true)
            : manager(manager_), name(name_), report_on_fail(report_on_fail_) {}
    AudioSinksManager* manager;
    std::string name;
    bool report_on_fail;
};

AudioSinksManager::AudioSinksManager(boost::asio::io_service& io_service_, const char* logger_name)
        : io_service(io_service_), pa_mainloop(io_service), error_handler(nullptr), running(true),
          stopping(false) {
    logger = spdlog::get(logger_name);
    pa_mainloop.get_strand().post([this] { start_pa_connection(); });
    pa_mainloop.set_loop_quit_callback([this](int retval) { mainloop_quit_handler(retval); });
}

void AudioSinksManager::set_error_handler(std::function<void(const std::string&)> error_handler_) {
    error_handler = error_handler_;
}

AudioSinksManager::~AudioSinksManager() {
    assert(!running && "Tried to destruct running instance of AudioSinksManager");
}

void AudioSinksManager::mainloop_quit_handler(int retval) {
    running = false;
    if (retval != 0) {
        report_error("PulseAudio mainloop stoped unexpectedly: " + std::to_string(retval));
    } else {
        logger->debug("(AudioSinkManager) Stopped running");
    }
}

void AudioSinksManager::start_pa_connection() {
    context = pa_context_new(pa_mainloop.get_api(), "chromecast-sink");
    if (!context) {
        report_error("Couldn't create context" + get_pa_error());
        return;
    }

    pa_context_set_state_callback(context, context_state_callback, this);
    pa_context_set_event_callback(context, context_event_callback, this);
    pa_context_set_subscribe_callback(context, context_subscription_callback, this);

    if (pa_context_connect(context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0) {
        report_error("Couldn't connect to PulseAudio server" + get_pa_error());
        return;
    }
}

void AudioSinksManager::context_state_callback(pa_context* c, void* userdata) {
    AudioSinksManager* manager = static_cast<AudioSinksManager*>(userdata);
    pa_context_state_t state = pa_context_get_state(c);

    const char* state_name;
    switch (state) {
        case PA_CONTEXT_UNCONNECTED: state_name = "UNCONNECTED"; break;
        case PA_CONTEXT_CONNECTING: state_name = "CONNECTING"; break;
        case PA_CONTEXT_AUTHORIZING: state_name = "AUTHORIZING"; break;
        case PA_CONTEXT_SETTING_NAME: state_name = "SETTING_NAME"; break;
        case PA_CONTEXT_READY: state_name = "READY"; break;
        case PA_CONTEXT_TERMINATED: state_name = "TERMINATED"; break;
        case PA_CONTEXT_FAILED: state_name = "FAILED"; break;
    }
    manager->logger->debug("(AudioSinkManager) PA state change: {}", state_name);

    switch (state) {
        case PA_CONTEXT_READY:
            manager->logger->info("(AudioSinkManager) Connected to PulseAudio server");
            if (!manager->stopping) {
                pa_operation_unref(pa_context_subscribe(
                        c, PA_SUBSCRIPTION_MASK_ALL, context_success_callback,
                        new ContextOperation(manager, "Subscripe to context events")));
                for (auto& sink : manager->audio_sinks) {
                    sink->start_sink();
                }
            } else {
                while (!manager->audio_sinks.empty()) {
                    auto sink = *manager->audio_sinks.begin();
                    sink->free();
                }
                manager->logger->trace("(AudioSinkManager) Disconnecting context");
                pa_context_disconnect(manager->context);
            }
            break;

        case PA_CONTEXT_TERMINATED:
            pa_context_unref(manager->context);
            manager->context = nullptr;
            (manager->pa_mainloop.get_api()->quit)(manager->pa_mainloop.get_api(), 0);
            break;

        case PA_CONTEXT_FAILED:
            manager->report_error("Connection to PulseAudio server failed: " +
                                  manager->get_pa_error());
            break;

        default: break;
    }
}

void AudioSinksManager::context_event_callback(pa_context* /*c*/, const char* name,
                                               pa_proplist* /*proplist*/, void* userdata) {
    AudioSinksManager* manager = static_cast<AudioSinksManager*>(userdata);
    manager->logger->debug("(AudioSinkManager) PulseAudio event: {}", name);
}

void AudioSinksManager::context_subscription_callback(pa_context* /*c*/,
                                                      pa_subscription_event_type_t t, uint32_t idx,
                                                      void* userdata) {
    AudioSinksManager* manager = static_cast<AudioSinksManager*>(userdata);
    const char *facility, *event_type;
    switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {
        case PA_SUBSCRIPTION_EVENT_SINK: facility = "Sink"; break;
        case PA_SUBSCRIPTION_EVENT_SOURCE: facility = "Source"; break;
        case PA_SUBSCRIPTION_EVENT_SINK_INPUT: facility = "Sink input"; break;
        case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT: facility = "Source output"; break;
        case PA_SUBSCRIPTION_EVENT_MODULE: facility = "Module"; break;
        case PA_SUBSCRIPTION_EVENT_CLIENT: facility = "Client"; break;
        case PA_SUBSCRIPTION_EVENT_SAMPLE_CACHE: facility = "Sample cache"; break;
        case PA_SUBSCRIPTION_EVENT_SERVER: facility = "Server"; break;
        case PA_SUBSCRIPTION_EVENT_CARD: facility = "Card"; break;
        default: assert(false && "Unexpected subscribtion facility type");
    }
    switch (t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) {
        case PA_SUBSCRIPTION_EVENT_NEW: event_type = "NEW"; break;
        case PA_SUBSCRIPTION_EVENT_CHANGE: event_type = "CHANGE"; break;
        case PA_SUBSCRIPTION_EVENT_REMOVE: event_type = "REMOVE"; break;
        default: assert(false && "Unexpected subscribtion event type");
    }
    manager->logger->trace("(AudioSinkManager) Subscription: {} {} {}", idx, facility, event_type);
}

void AudioSinksManager::context_success_callback(pa_context* /*c*/, int success, void* userdata) {
    ContextOperation* op = static_cast<ContextOperation*>(userdata);
    if (!success) {
        std::stringstream msg;
        msg << "Operation '" << op->name << "' on context failed";
        if (op->report_on_fail) {
            op->manager->report_error(msg.str());
        } else {
            op->manager->logger->error("(AudioSinkManager) {}", msg.str());
        }
    }
}

void AudioSinksManager::report_error(const std::string& message) {
    running = false;
    if (error_handler) {
        error_handler(message);
    } else {
        throw AudioSinksManagerException(message);
    }
}

std::string AudioSinksManager::get_pa_error() const {
    return pa_strerror(pa_context_errno(context));
}

void AudioSinksManager::stop() {
    pa_mainloop.get_strand().dispatch([this] {
        if (stopping) return;
        logger->trace("(AudioSinkManager) Stopping");
        stopping = true;
        if (audio_sinks.empty()) {
            if (context) {
                logger->trace("(AudioSinkManager) Disconnect context");
                pa_context_disconnect(context);
            }
        } else {
            for (const auto& sink : audio_sinks) {
                sink->free();
            }
        }
    });
}

std::shared_ptr<AudioSink> AudioSinksManager::create_new_sink(std::string name) {
    auto internal_sink =
            std::shared_ptr<InternalAudioSink>(new InternalAudioSink(this, std::move(name)));
    auto sink = std::shared_ptr<AudioSink>(new AudioSink(internal_sink));
    pa_mainloop.get_strand().dispatch([this, internal_sink]() {
        if (stopping) {
            internal_sink->free();
        } else {
            logger->trace("(AudioSinkManager) Registering audio_sink '{}'",
                          internal_sink->get_name());
            audio_sinks.insert(internal_sink);
            if (pa_context_get_state(context) == PA_CONTEXT_READY) {
                internal_sink->start_sink();
            }
        }
    });
    return sink;
}

void AudioSinksManager::unregister_audio_sink(std::shared_ptr<InternalAudioSink> sink) {
    logger->trace("(AudioSinkManager) Unregistering audio_sink '{}'", sink->get_name());
    audio_sinks.erase(sink);
    if (audio_sinks.empty() && stopping) {
        if (context) {
            logger->trace("(AudioSinkManager) Disconnect context");
            pa_context_disconnect(context);
        }
    }
}

AudioSinksManager::InternalAudioSink::InternalAudioSink(AudioSinksManager* manager_,
                                                        std::string name_)
        : manager(manager_), name(name_), state(State::NONE) {
    identifier = generate_random_string(10);
}

const std::string& AudioSinksManager::InternalAudioSink::get_name() const {
    return name;
}

AudioSinksManager::InternalAudioSink::~InternalAudioSink() {
    assert(state == State::DEAD);
}

void AudioSinksManager::InternalAudioSink::free() {
    assert(manager->pa_mainloop.get_strand().running_in_this_thread());
    static const char* state_name[] = {"NONE", "STARTED", "LOADED", "RECORDING", "DEAD"};
    manager->logger->trace("(AudioSink '{}') Freeing, state: {}", name,
                           state_name[static_cast<int>(state)]);
    switch (state) {
        case State::NONE: manager->unregister_audio_sink(shared_from_this()); break;
        case State::STARTED: /* Handled in module_load_callback */ break;
        case State::LOADED: stop_sink(); break;
        case State::RECORDING:
            if (pa_stream_disconnect(stream) < 0) {
                manager->logger->error("(AudioSink '{}') Failed to start disconnecting stream {}",
                                       name, module_idx);
                pa_stream_unref(stream);
                stream = nullptr;
                stop_sink();
            }
            break;
        case State::DEAD: return;
    }
    state = State::DEAD;
}

void AudioSinksManager::InternalAudioSink::start_sink() {
    assert(manager->pa_mainloop.get_strand().running_in_this_thread());
    assert(state == State::NONE);
    manager->logger->trace("(AudioSink '{}') Starting sink", name);
    state = State::STARTED;
    std::string escaped_name =
            replace_all(replace_all(replace_all(name, "\\", "\\\\"), " ", "\\ "), "\"", "\\\"");
    std::stringstream arguments;
    arguments << "sink_name=" << identifier << " sink_properties=device.description=\""
              << escaped_name << "\"";
    pa_operation* op = pa_context_load_module(manager->context, "module-null-sink",
                                              arguments.str().c_str(), module_load_callback, this);
    if (op) {
        pa_operation_unref(op);
    } else {
        manager->logger->error("(AudioSink '{}') Failed to start loading module", name);
        state = State::DEAD;
        manager->unregister_audio_sink(shared_from_this());
    }
}

void AudioSinksManager::InternalAudioSink::stop_sink() {
    manager->logger->trace("(AudioSink '{}') Stopping sink", name);
    pa_operation* op =
            pa_context_unload_module(manager->context, module_idx, module_unload_callback, this);
    if (op) {
        pa_operation_unref(op);
    } else {
        manager->logger->error(
                "(AudioSink '{}') Failed to start unloading start unloading module {}", name,
                module_idx);
        state = State::DEAD;
        manager->unregister_audio_sink(shared_from_this());
    }
}

void AudioSinksManager::InternalAudioSink::module_load_callback(pa_context* /*c*/, uint32_t idx,
                                                                void* userdata) {
    AudioSinksManager::InternalAudioSink* sink =
            static_cast<AudioSinksManager::InternalAudioSink*>(userdata);
    if (idx == static_cast<uint32_t>(-1)) {
        sink->manager->logger->error("(AudioSink '{}') Failed to load module {}: ", sink->name, idx,
                                     sink->manager->get_pa_error());
        sink->state = State::DEAD;
        sink->manager->unregister_audio_sink(sink->shared_from_this());
        return;
    }

    sink->module_idx = idx;
    sink->manager->logger->debug("(AudioSink '{}') Loaded module idx: {}, name: {}", sink->name,
                                 sink->module_idx, sink->identifier);
    if (sink->state == State::DEAD) {
        sink->stop_sink();
        return;
    }

    sink->state = State::LOADED;

    // Start record stream
    pa_sample_spec sample_spec;
    sample_spec.format = PA_SAMPLE_S16LE;
    sample_spec.channels = 2;
    sample_spec.rate = 44100;
    std::string stream_name = sink->identifier + "_record_stream";
    sink->stream = pa_stream_new(sink->manager->context, stream_name.c_str(), &sample_spec, NULL);
    if (!sink->stream) {
        sink->manager->logger->error("(AudioSink '{}') Failed to create stream: {}", sink->name,
                                     sink->manager->get_pa_error());
        sink->free();
        return;
    }

    pa_stream_set_state_callback(sink->stream, stream_state_change_callback, sink);
    pa_stream_set_read_callback(sink->stream, stream_read_callback, sink);

    std::string device_name = sink->identifier + ".monitor";
    pa_buffer_attr buffer_attr;
    buffer_attr.fragsize =
            (sample_spec.rate * sizeof(AudioSample)) / (1000 / 20);  // 20ms of buffer
    buffer_attr.maxlength = static_cast<uint32_t>(-1);
    buffer_attr.minreq = buffer_attr.prebuf = buffer_attr.tlength =
            static_cast<uint32_t>(-1);  // playback only arguments
    pa_stream_flags_t stream_flags = static_cast<pa_stream_flags_t>(
            PA_STREAM_DONT_MOVE | PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING |
            PA_STREAM_START_UNMUTED | PA_STREAM_ADJUST_LATENCY);
    if (pa_stream_connect_record(sink->stream, device_name.c_str(), &buffer_attr, stream_flags) <
        0) {
        pa_stream_unref(sink->stream);
        sink->stream = nullptr;
        sink->manager->logger->error("(AudioSink '{}') Failed to connect to stream: {}", sink->name,
                                     sink->manager->get_pa_error());
        sink->free();
        return;
    }

    sink->state = State::RECORDING;
}

void AudioSinksManager::InternalAudioSink::module_unload_callback(pa_context* /*c*/, int success,
                                                                  void* userdata) {
    AudioSinksManager::InternalAudioSink* sink =
            static_cast<AudioSinksManager::InternalAudioSink*>(userdata);
    if (!success) {
        sink->manager->logger->error("(AudioSink '{}') Failed to unload module {}: {}", sink->name,
                                     sink->module_idx, sink->manager->get_pa_error());
    } else {
        sink->manager->logger->debug("(AudioSink '{}') Unloaded module {}", sink->name,
                                     sink->module_idx);
    }
    sink->manager->unregister_audio_sink(sink->shared_from_this());
}

void AudioSinksManager::InternalAudioSink::stream_state_change_callback(pa_stream* /*stream*/,
                                                                        void* userdata) {
    AudioSinksManager::InternalAudioSink* sink =
            static_cast<AudioSinksManager::InternalAudioSink*>(userdata);
    pa_stream_state_t state = pa_stream_get_state(sink->stream);
    const char* state_str;
    switch (state) {
        case PA_STREAM_UNCONNECTED: state_str = "UNCONNECTED"; break;
        case PA_STREAM_CREATING: state_str = "CREATING"; break;
        case PA_STREAM_READY: state_str = "READY"; break;
        case PA_STREAM_FAILED: state_str = "FAILED"; break;
        case PA_STREAM_TERMINATED: state_str = "TERMINATED"; break;
    }
    sink->manager->logger->debug("(AudioSink '{}') Stream new state: {}", sink->name, state_str);

    switch (state) {
        case PA_STREAM_FAILED:
            sink->manager->logger->error("(AudioSink '{}') Stream failed: {}", sink->name,
                                         sink->manager->get_pa_error());
            sink->state = State::DEAD;
        case PA_STREAM_TERMINATED:
            pa_stream_unref(sink->stream);
            sink->stream = nullptr;
            sink->stop_sink();
            break;
        default: break;
    }
}

void AudioSinksManager::InternalAudioSink::stream_read_callback(pa_stream* /*stream*/,
                                                                size_t /*nbytes*/, void* userdata) {
    AudioSinksManager::InternalAudioSink* sink =
            static_cast<AudioSinksManager::InternalAudioSink*>(userdata);

    const void* data;
    size_t data_size;
    if (pa_stream_peek(sink->stream, &data, &data_size) < 0) {
        sink->manager->logger->error("(AudioSink '{}') Failed to read data from stream: ",
                                     sink->name, sink->manager->get_pa_error());
        return;
    }

    if (data_size % sizeof(AudioSample) != 0) {
        sink->manager->logger->warn("(AudioSink '{}') Not rounded sample data in buffer");
    }

    if (data_size == 0) {
        return;
    } else if (data == NULL) {
        sink->manager->logger->trace("(AudioSink '{}') There is a hole in a record stream!");
    }

    if (sink->samples_callback) {
        sink->samples_callback(static_cast<const AudioSample*>(data),
                               data_size / sizeof(AudioSample));
    }

    if (pa_stream_drop(sink->stream) < 0) {
        sink->manager->logger->error("(AudioSink '{}') Failed to drop data from stream: ",
                                     sink->name, sink->manager->get_pa_error());
    }
}
void AudioSinksManager::InternalAudioSink::set_samples_callback(SamplesCallback samples_callback_) {
    assert(manager->pa_mainloop.get_strand().running_in_this_thread());
    samples_callback = samples_callback_;
}

AudioSink::AudioSink(std::shared_ptr<AudioSinksManager::InternalAudioSink> internal_audio_sink_)
        : internal_audio_sink(internal_audio_sink_) {}

AudioSink::~AudioSink() {
    internal_audio_sink->manager->pa_mainloop.get_strand().dispatch([sink = internal_audio_sink]() {
        sink->free();
    });
}

void AudioSink::set_samples_callback(
        AudioSinksManager::InternalAudioSink::SamplesCallback samples_callback) {
    internal_audio_sink->manager->pa_mainloop.get_strand().dispatch([
        sink = internal_audio_sink, samples_callback
    ] { sink->set_samples_callback(samples_callback); });
}
