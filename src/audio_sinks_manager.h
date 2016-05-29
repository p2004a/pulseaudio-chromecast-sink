/* audio_sinks_manager.h -- This file is part of pulseaudio-chromecast-sink
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

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include <pulse/context.h>
#include <pulse/introspect.h>
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
        typedef std::function<void(double, double, bool)> VolumeCallback;
        typedef std::function<void(bool)> ActivationCallback;

        InternalAudioSink(AudioSinksManager* manager_, std::string name_);
        ~InternalAudioSink();

        const std::string& get_name() const;
        const std::string& get_identifier() const;
        uint32_t get_sink_idx() const;
        void set_samples_callback(SamplesCallback);
        void set_activation_callback(ActivationCallback);
        void set_volume_callback(VolumeCallback);
        void free(bool user = false);
        void start_sink();

        void set_is_default_sink(bool b);
        void update_sink_inputs_num(int difference);
        void update_sink_info();

      private:
        enum class State { NONE, STARTED, LOADED, RECORDING, DEAD };

        static void module_load_callback(pa_context* c, uint32_t idx, void* userdata);
        static void module_unload_callback(pa_context* c, int success, void* userdata);
        static void stream_state_change_callback(pa_stream* stream, void* userdata);
        static void stream_read_callback(pa_stream* stream, size_t nbytes, void* userdata);
        void stop_sink();
        static void sink_info_callback(pa_context* c, const pa_sink_info* info, int eol,
                                       void* userdata);
        void update_activated();

        AudioSinksManager* manager;
        SamplesCallback samples_callback;
        ActivationCallback activation_callback;
        VolumeCallback volume_callback;
        pa_stream* stream;
        std::string name, identifier;
        uint32_t module_idx, sink_idx;
        pa_cvolume volume;
        bool muted;
        State state;
        bool default_sink, activated;
        int num_sink_inputs;

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
    static void sink_input_info_callback(pa_context* c, const pa_sink_input_info* info, int eol,
                                         void* userdata);
    static void server_info_callback(pa_context* c, const pa_server_info* info, void* userdata);
    void update_sink_input_map(uint32_t sink, pa_subscription_event_type_t event_type);
    void update_server_info();
    uint32_t remove_input_sink(uint32_t input_idx);
    void add_input_sink(uint32_t input_idx, uint32_t sink_idx);
    void try_update_sink_input_nums(uint32_t sink_idx, int difference);

    void report_error(const std::string& message);
    std::string get_pa_error() const;
    void mainloop_quit_handler(int retval);

    boost::asio::io_service& io_service;
    std::shared_ptr<spdlog::logger> logger;
    AsioPulseAudioMainloop pa_mainloop;
    std::function<void(const std::string&)> error_handler;
    // insert in AudioSinksManager::create_new_sink,
    // remove in AudioSinksManager::unregister_audio_sink
    std::unordered_set<std::shared_ptr<InternalAudioSink>> audio_sinks;
    // insert in InternalAudioSink::sink_info_callback,
    // remove in AudioSinksManager::unregister_audio_sink
    std::unordered_map<uint32_t, std::shared_ptr<InternalAudioSink>> sink_idx_audio_sink;
    // insert in AudioSinksManager::add_input_sink
    // remove in AudioSinksManager::remove_input_sink
    std::unordered_map<uint32_t, uint32_t> sink_inputs_sinks;
    // insert in AudioSinksManager::create_new_sink
    // remove in AudioSinksManager::unregister_audio_sink
    std::unordered_map<std::string, std::shared_ptr<InternalAudioSink>> sink_identifier_audio_sink;
    pa_context* context;
    std::string default_sink_name;
    bool running, stopping;

    friend class AudioSink;
};

class AudioSink {
  public:
    AudioSink(const AudioSink&) = delete;

    ~AudioSink();

    void set_samples_callback(AudioSinksManager::InternalAudioSink::SamplesCallback);
    void set_activation_callback(AudioSinksManager::InternalAudioSink::ActivationCallback);
    void set_volume_callback(AudioSinksManager::InternalAudioSink::VolumeCallback);

  private:
    AudioSink(std::shared_ptr<AudioSinksManager::InternalAudioSink> internal_audio_sink_);

    std::shared_ptr<AudioSinksManager::InternalAudioSink> internal_audio_sink;

    friend class AudioSinksManager;
};
