#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include <pulse/context.h>
#include <pulse/stream.h>

#include <boost/asio/io_service.hpp>

#include "asio_pa_mainloop_api.h"

struct AudioSample {
    int16_t left, right;
};

class AudioSinksManagerException : public std::runtime_error {
  public:
    AudioSinksManagerException(std::string message) : std::runtime_error(message) {}
};

class AudioSink;

class AudioSinksManager {
  public:
    AudioSinksManager(boost::asio::io_service& io_service_, const char* logger_name = "default");

    ~AudioSinksManager();

    void stop();

    // This function have to be called *before* starting event loop!
    void set_error_handler(std::function<void(const std::string&)>);

    std::shared_ptr<AudioSink> create_new_sink(std::string name);

  private:
    class InternalAudioSink : public std::enable_shared_from_this<InternalAudioSink> {
      public:
        typedef std::function<void(const AudioSample*, size_t)> SamplesCallback;

        InternalAudioSink(AudioSinksManager* manager_, std::string name_);
        ~InternalAudioSink();

        const std::string& get_name() const;
        void set_samples_callback(SamplesCallback);
        void free();
        void start_sink();

      private:
        enum class State { NONE, STARTED, LOADED, RECORDING, DEAD };

        static void module_load_callback(pa_context* c, uint32_t idx, void* userdata);
        static void module_unload_callback(pa_context* c, int success, void* userdata);
        static void stream_state_change_callback(pa_stream* stream, void* userdata);
        static void stream_read_callback(pa_stream* stream, size_t nbytes, void* userdata);
        void stop_sink();

        AudioSinksManager* manager;
        SamplesCallback samples_callback;
        pa_stream* stream;
        std::string name, identifier;
        uint32_t module_idx;
        State state;

        friend class AudioSink;
    };

    void start_pa_connection();
    void unregister_audio_sink(std::shared_ptr<InternalAudioSink> sink);
    static void context_state_callback(pa_context* c, void* userdata);
    static void context_event_callback(pa_context* c, const char* name, pa_proplist* proplist,
                                       void* userdata);
    static void context_subscription_callback(pa_context* c, pa_subscription_event_type_t t,
                                              uint32_t idx, void* userdata);
    static void context_success_callback(pa_context* c, int success, void* userdata);

    void report_error(const std::string& message);
    std::string get_pa_error() const;
    void mainloop_quit_handler(int retval);

    boost::asio::io_service& io_service;
    std::shared_ptr<spdlog::logger> logger;
    AsioPulseAudioMainloop pa_mainloop;
    std::function<void(const std::string&)> error_handler;
    std::unordered_set<std::shared_ptr<InternalAudioSink>> audio_sinks;
    pa_context* context;
    bool running, stopping;

    friend class AudioSink;
};

class AudioSink {
  public:
    AudioSink(const AudioSink&) = delete;

    ~AudioSink();

    void set_samples_callback(AudioSinksManager::InternalAudioSink::SamplesCallback);
    // TODO: void set_activation_callback(std::function<void(bool)>);
    // TODO: void set_volume_change_callback(std::function<void(double)>);

  private:
    AudioSink(std::shared_ptr<AudioSinksManager::InternalAudioSink> internal_audio_sink_);

    std::shared_ptr<AudioSinksManager::InternalAudioSink> internal_audio_sink;

    friend class AudioSinksManager;
};
