/* asio_pa_mainloop_api.cpp -- This file is part of pulseaudio-chromecast-sink
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

#include <cassert>

#include <boost/asio/io_service.hpp>

#include "asio_pa_mainloop_api.h"
#include "generic_loop_api.h"

typedef IOEvent<void*, pa_mainloop_api*> PAIOEvent;
typedef TimerEvent<void*, pa_mainloop_api*> PATimerEvent;
typedef DeferedEvent<void*, pa_mainloop_api*> PADeferedEvent;

IOEventFlags map_io_flags_from_pa(pa_io_event_flags_t pa_flags) {
    IOEventFlags flags = IOEventFlags::NONE;
    if (pa_flags & PA_IO_EVENT_ERROR) flags |= IOEventFlags::ERROR;
    if (pa_flags & PA_IO_EVENT_INPUT) flags |= IOEventFlags::INPUT;
    if (pa_flags & PA_IO_EVENT_OUTPUT) flags |= IOEventFlags::OUTPUT;
    if (pa_flags & PA_IO_EVENT_HANGUP) flags |= IOEventFlags::HANGUP;
    return flags;
}

pa_io_event_flags_t map_io_flags_to_pa(IOEventFlags flags) {
    pa_io_event_flags_t pa_flags = PA_IO_EVENT_NULL;
    if ((flags & IOEventFlags::ERROR) != IOEventFlags::NONE)
        pa_flags = static_cast<pa_io_event_flags_t>(pa_flags | PA_IO_EVENT_ERROR);
    if ((flags & IOEventFlags::INPUT) != IOEventFlags::NONE)
        pa_flags = static_cast<pa_io_event_flags_t>(pa_flags | PA_IO_EVENT_INPUT);
    if ((flags & IOEventFlags::OUTPUT) != IOEventFlags::NONE)
        pa_flags = static_cast<pa_io_event_flags_t>(pa_flags | PA_IO_EVENT_OUTPUT);
    if ((flags & IOEventFlags::HANGUP) != IOEventFlags::NONE)
        pa_flags = static_cast<pa_io_event_flags_t>(pa_flags | PA_IO_EVENT_HANGUP);
    return pa_flags;
}

pa_io_event* AsioPulseAudioMainloop::io_new(pa_mainloop_api* a, int fd, pa_io_event_flags_t events,
                                            pa_io_event_cb_t cb, void* userdata) {
    auto api = static_cast<AsioPulseAudioMainloop*>(a->userdata);
    assert(api->strand.running_in_this_thread());
    auto io_event = new PAIOEvent(api->strand, api->io_service, fd, userdata, a,
                                  [cb](PAIOEvent* event, int fd_, IOEventFlags flags,
                                       void* userdata_, pa_mainloop_api* api_) {
                                      cb(api_, reinterpret_cast<pa_io_event*>(event), fd_,
                                         map_io_flags_to_pa(flags), userdata_);
                                  });
    io_event->update(map_io_flags_from_pa(events));
    return reinterpret_cast<pa_io_event*>(io_event);
}

void AsioPulseAudioMainloop::io_enable(pa_io_event* e, pa_io_event_flags_t events) {
    reinterpret_cast<PAIOEvent*>(e)->update(map_io_flags_from_pa(events));
}

void AsioPulseAudioMainloop::io_free(pa_io_event* e) {
    reinterpret_cast<PAIOEvent*>(e)->free();
}

void AsioPulseAudioMainloop::io_set_destroy(pa_io_event* e, pa_io_event_destroy_cb_t cb) {
    reinterpret_cast<PAIOEvent*>(e)->set_destroy_callback(
            [cb](PAIOEvent* event, void* userdata, pa_mainloop_api* api_) {
                cb(api_, reinterpret_cast<pa_io_event*>(event), userdata);
            });
}

pa_time_event* AsioPulseAudioMainloop::time_new(pa_mainloop_api* a, const struct timeval* tv,
                                                pa_time_event_cb_t cb, void* userdata) {
    auto api = static_cast<AsioPulseAudioMainloop*>(a->userdata);
    assert(api->strand.running_in_this_thread());
    auto time_event =
            new PATimerEvent(api->strand, api->io_service, userdata, a,
                             [cb](PATimerEvent* event, const struct timeval* tv, void* userdata_,
                                  pa_mainloop_api* api_) {
                                 cb(api_, reinterpret_cast<pa_time_event*>(event), tv, userdata_);
                             });
    time_event->update(tv);
    return reinterpret_cast<pa_time_event*>(time_event);
}

void AsioPulseAudioMainloop::time_restart(pa_time_event* e, const struct timeval* tv) {
    reinterpret_cast<PATimerEvent*>(e)->update(tv);
}

void AsioPulseAudioMainloop::time_free(pa_time_event* e) {
    reinterpret_cast<PATimerEvent*>(e)->free();
}

void AsioPulseAudioMainloop::time_set_destroy(pa_time_event* e, pa_time_event_destroy_cb_t cb) {
    reinterpret_cast<PATimerEvent*>(e)->set_destroy_callback(
            [cb](PATimerEvent* event, void* userdata, pa_mainloop_api* api) {
                cb(api, reinterpret_cast<pa_time_event*>(event), userdata);
            });
}

pa_defer_event* AsioPulseAudioMainloop::defer_new(pa_mainloop_api* a, pa_defer_event_cb_t cb,
                                                  void* userdata) {
    auto api = static_cast<AsioPulseAudioMainloop*>(a->userdata);
    assert(api->strand.running_in_this_thread());
    auto defered_event =
            new PADeferedEvent(api->strand, userdata, a,
                               [cb](PADeferedEvent* event, void* userdata_, pa_mainloop_api* api_) {
                                   cb(api_, reinterpret_cast<pa_defer_event*>(event), userdata_);
                               });
    defered_event->update(true);
    return reinterpret_cast<pa_defer_event*>(defered_event);
}

void AsioPulseAudioMainloop::defer_enable(pa_defer_event* e, int b) {
    reinterpret_cast<PADeferedEvent*>(e)->update(b != 0);
}

void AsioPulseAudioMainloop::defer_free(pa_defer_event* e) {
    reinterpret_cast<PADeferedEvent*>(e)->free();
}

void AsioPulseAudioMainloop::defer_set_destroy(pa_defer_event* e, pa_defer_event_destroy_cb_t cb) {
    reinterpret_cast<PADeferedEvent*>(e)->set_destroy_callback(
            [cb](PADeferedEvent* event, void* userdata, pa_mainloop_api* api) {
                cb(api, reinterpret_cast<pa_defer_event*>(event), userdata);
            });
}

void AsioPulseAudioMainloop::quit(pa_mainloop_api* a, int retval) {
    auto api = static_cast<AsioPulseAudioMainloop*>(a->userdata);
    api->handle_loop_quit(retval);
}

void AsioPulseAudioMainloop::handle_loop_quit(int retval) const {
    if (loop_quit_callback) {
        loop_quit_callback(retval);
    } else {
        throw AsioPulseAudioMailoopUnexpectedEnd(retval);
    }
}

AsioPulseAudioMainloop::AsioPulseAudioMainloop(boost::asio::io_service& io_service_)
        : io_service(io_service_), strand(io_service), loop_quit_callback(nullptr) {
    api.userdata = this;
    api.io_new = io_new;
    api.io_enable = io_enable;
    api.io_free = io_free;
    api.io_set_destroy = io_set_destroy;
    api.time_new = time_new;
    api.time_restart = time_restart;
    api.time_free = time_free;
    api.time_set_destroy = time_set_destroy;
    api.defer_new = defer_new;
    api.defer_enable = defer_enable;
    api.defer_free = defer_free;
    api.defer_set_destroy = defer_set_destroy;
    api.quit = quit;
}
