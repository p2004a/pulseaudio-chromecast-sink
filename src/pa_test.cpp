/* pa_test.cpp -- This file is part of pulseaudio-chromecast-sink
 * Copyright (C) 2016  Marek Rusinowski
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include <cerrno>
#include <cstring>

#include <sched.h>
#include <sys/types.h>
#include <unistd.h>

#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/mainloop.h>
#include <pulse/stream.h>

#include <boost/asio.hpp>

#include "asio_pa_mainloop_api.h"
#include "defer.h"

struct sin_wave_sound {
    uint64_t iteration;
    uint64_t num_iterations;
    int freq;
};

class pa_error : public std::runtime_error {
  public:
    pa_error(std::string message) : std::runtime_error(message) {}

    pa_error(pa_context* context, std::string message)
            : pa_error(message + ": " + std::string(pa_strerror(pa_context_errno(context)))) {}

    pa_error(pa_stream* stream, std::string message)
            : pa_error(pa_stream_get_context(stream), message) {}
};

void done_writing(pa_stream* stream, int success, void* /*userdata*/) {
    std::cerr << "Done writing!" << std::endl;

    if (!success) {
        throw pa_error("Failed done_writing callback");
    }

    if (pa_stream_disconnect(stream) < 0) {
        throw pa_error(stream, "Failed to disconnect stream");
    }
    pa_stream_unref(stream);

    pa_context* context = pa_stream_get_context(stream);
    pa_context_disconnect(context);
}

void stream_write_callback(pa_stream* stream, size_t nbytes, void* userdata) {
    typedef std::chrono::high_resolution_clock clock;

    auto start = clock::now();

    sin_wave_sound* sound = static_cast<sin_wave_sound*>(userdata);
    if (sound->iteration == sound->num_iterations) {
        return;
    }

    size_t to_write =
            std::min(nbytes, (sound->num_iterations - sound->iteration) * 2 * sizeof(float));
    while (to_write > 0) {
        float* buffer;
        size_t buffer_size = to_write;
        if (pa_stream_begin_write(stream, reinterpret_cast<void**>(&buffer), &buffer_size) < 0) {
            throw pa_error(stream, "Failed to allocate write buffer");
        }

        const uint64_t num_iterations = buffer_size / (2 * sizeof(float));
        buffer_size = num_iterations * 2 * sizeof(float);
        double freq_const = (2.0 * M_PI * static_cast<double>(sound->freq)) / 44100.0;
        for (uint64_t i = 0; i < num_iterations; ++i) {
            float sample =
                    static_cast<float>(sin(static_cast<double>(sound->iteration + i) * freq_const));
            buffer[i * 2 + 0] = buffer[i * 2 + 1] = sample;
        }
        sound->iteration += num_iterations;

        if (pa_stream_write(stream, static_cast<void*>(buffer), buffer_size, NULL, 0,
                            PA_SEEK_RELATIVE) < 0) {
            throw pa_error(stream, "Failed to write buffer to stream");
        }

        to_write -= buffer_size;
    }

    if (sound->iteration == sound->num_iterations) {
        pa_operation_unref(pa_stream_drain(stream, done_writing, NULL));
    }

    auto end = clock::now();

    std::cout << "duration: "
              << static_cast<std::chrono::duration<float, std::chrono::milliseconds::period>>(end -
                                                                                              start)
                         .count()
              << std::endl;
}

void stream_underflow_callback(pa_stream* /*stream*/, void* /*userdata*/) {
    std::cerr << "stream underflow" << std::endl;
}

void stream_overflow_callback(pa_stream* /*stream*/, void* /*userdata*/) {
    std::cerr << "stream overflow" << std::endl;
}

void stream_started_callback(pa_stream* /*stream*/, void* /*userdata*/) {
    std::cerr << "stream started" << std::endl;
}

void stream_event_callback(pa_stream* /*stream*/, const char* /*name*/, pa_proplist* /*pl*/,
                           void* /*userdata*/) {
    std::cerr << "stream event" << std::endl;
}

void stream_state_change(pa_stream* stream, void* /*userdata*/) {
    pa_stream_state_t state = pa_stream_get_state(stream);
    const char* state_str;
    switch (state) {
        case PA_STREAM_UNCONNECTED: state_str = "UNCONNECTED"; break;
        case PA_STREAM_CREATING: state_str = "CREATING"; break;
        case PA_STREAM_READY: state_str = "READY"; break;
        case PA_STREAM_FAILED: state_str = "FAILED"; break;
        case PA_STREAM_TERMINATED: state_str = "TERMINATED"; break;
    }
    std::cerr << "stream new state: " << state_str << std::endl;
}

void create_stream(pa_context* context) {
    pa_sample_spec sample_spec;
    sample_spec.channels = 2;
    sample_spec.format = PA_SAMPLE_FLOAT32LE;
    sample_spec.rate = 44100;
    pa_channel_map channel_map;
    channel_map.channels = 2;
    channel_map.map[0] = PA_CHANNEL_POSITION_FRONT_LEFT;
    channel_map.map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT;
    pa_stream* stream = pa_stream_new(context, "Sin Wave", &sample_spec, &channel_map);
    if (!stream) {
        throw pa_error(context, "Failed to create stream");
    }

    sin_wave_sound* sound = new sin_wave_sound;
    sound->iteration = 0;
    sound->num_iterations = 44100 * 10;  // 5sec
    sound->freq = 3000;

    pa_stream_set_event_callback(stream, stream_event_callback, NULL);
    pa_stream_set_started_callback(stream, stream_started_callback, NULL);
    pa_stream_set_state_callback(stream, stream_state_change, NULL);
    pa_stream_set_write_callback(stream, stream_write_callback, sound);
    pa_stream_set_underflow_callback(stream, stream_underflow_callback, NULL);
    pa_stream_set_overflow_callback(stream, stream_overflow_callback, NULL);

    pa_buffer_attr buffer_attr;
    buffer_attr.maxlength = static_cast<uint32_t>(-1);
    double num_milis = 20.0;
    buffer_attr.tlength =
            static_cast<uint32_t>(ceil(44100.0 * (num_milis / 1000.0))) * (2 * sizeof(float));
    buffer_attr.prebuf = buffer_attr.tlength / 2;
    buffer_attr.minreq = static_cast<uint32_t>(-1);
    buffer_attr.fragsize = static_cast<uint32_t>(-1);
    if (pa_stream_connect_playback(stream, NULL, &buffer_attr, PA_STREAM_NOFLAGS, NULL, NULL) < 0) {
        throw pa_error(context, "Failed to connect stream");
    }
}

void context_event_callback(pa_context* /*context*/, const char* /*name*/,
                            pa_proplist* /*proplist*/, void* /*userdata*/) {
    std::cerr << "event!" << std::endl;
}

void context_state_callback(pa_context* context, void* userdata) {
    pa_mainloop_api* loop_api = static_cast<pa_mainloop_api*>(userdata);
    pa_context_state_t state = pa_context_get_state(context);
    switch (state) {
        case PA_CONTEXT_READY:
            std::cerr << "PA: Context ready!" << std::endl;
            create_stream(context);
            // pa_context_disconnect(context);
            break;
        case PA_CONTEXT_TERMINATED:
            std::cerr << "PA: Connection terminated cleanly." << std::endl;
            (loop_api->quit)(loop_api, 0);
            break;
        case PA_CONTEXT_FAILED: throw pa_error(context, "Connection to PulseAudio serwer failed");
        default: break;
    }
}

void start_stuff(pa_mainloop_api* loop_api, pa_context** context) {
    *context = pa_context_new(loop_api, "PA Test");
    if (!*context) {
        throw pa_error("Couldn't create context");
    }

    pa_context_set_state_callback(*context, context_state_callback, loop_api);
    pa_context_set_event_callback(*context, context_event_callback, NULL);

    if (pa_context_connect(*context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        throw pa_error(*context, "Couldn't connect to PulseAudio server");
    }
}

#define USE_ASIO_LOOP 1

int main() {
    pa_context* context;
#if USE_ASIO_LOOP
    boost::asio::io_service io_service;
    AsioPulseAudioMainloop loop(io_service);
    loop.set_loop_quit_callback([](int retval) {
        if (retval != 0) {
            throw pa_error("Main loop failed: " + std::to_string(retval));
        }
    });
    pa_mainloop_api* loop_api = loop.get_api();
    loop.get_strand().post([&] { start_stuff(loop_api, &context); });
    io_service.run();
#else
    pa_mainloop* loop = pa_mainloop_new();
    defer {
        pa_mainloop_free(loop);
    };
    pa_mainloop_api* loop_api = pa_mainloop_get_api(loop);
    start_stuff(loop_api, &context);
    int retval;
    if (pa_mainloop_run(loop, &retval) < 0) {
        throw pa_error("Main loop failes: " + std::to_string(retval));
    }
#endif
    pa_context_unref(context);
    return 0;
}
