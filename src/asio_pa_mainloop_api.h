/* asio_pa_mainloop_api.h -- This file is part of pulseaudio-chromecast-sink
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

#include <functional>
#include <stdexcept>

#include <pulse/mainloop-api.h>

#include <asio/io_service.hpp>
#include <asio/strand.hpp>

class AsioPulseAudioMailoopUnexpectedEnd : public std::runtime_error {
  public:
    AsioPulseAudioMailoopUnexpectedEnd(int retval_)
            : std::runtime_error("PulseAudio mailoop api unexpectedly quit"), retval(retval_) {}

    int get_retval() const {
        return retval;
    }

  private:
    int retval;
};

class AsioPulseAudioMainloop {
  public:
    AsioPulseAudioMainloop(const AsioPulseAudioMainloop&) = delete;
    AsioPulseAudioMainloop(asio::io_service& io_service_);

    pa_mainloop_api* get_api() {
        return &api;
    }

    asio::io_service::strand& get_strand() {
        return strand;
    }

    template <class Func>
    void set_loop_quit_callback(Func f) {
        loop_quit_callback = [f](int retval) { f(retval); };
    }

  private:
    void handle_loop_quit(int retval) const;

    static pa_io_event* io_new(pa_mainloop_api* a, int fd, pa_io_event_flags_t events,
                               pa_io_event_cb_t cb, void* userdata);
    static void io_enable(pa_io_event* e, pa_io_event_flags_t events);
    static void io_free(pa_io_event* e);
    static void io_set_destroy(pa_io_event* e, pa_io_event_destroy_cb_t cb);
    static pa_time_event* time_new(pa_mainloop_api* a, const struct timeval* tv,
                                   pa_time_event_cb_t cb, void* userdata);
    static void time_restart(pa_time_event* e, const struct timeval* tv);
    static void time_free(pa_time_event* e);
    static void time_set_destroy(pa_time_event* e, pa_time_event_destroy_cb_t cb);
    static pa_defer_event* defer_new(pa_mainloop_api* a, pa_defer_event_cb_t cb, void* userdata);
    static void defer_enable(pa_defer_event* e, int b);
    static void defer_free(pa_defer_event* e);
    static void defer_set_destroy(pa_defer_event* e, pa_defer_event_destroy_cb_t cb);
    static void quit(pa_mainloop_api* a, int retval);

    asio::io_service& io_service;
    asio::io_service::strand strand;
    pa_mainloop_api api;
    std::function<void(int)> loop_quit_callback;

    friend struct pa_io_event;
    friend struct pa_time_event;
    friend struct pa_defer_event;
};
