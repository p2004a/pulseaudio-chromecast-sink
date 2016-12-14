/* generic_loop_api_impl.h -- This file is part of pulseaudio-chromecast-sink
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

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <cassert>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>

#include <asio/generic/stream_protocol.hpp>
#include <asio/io_service.hpp>

#include "generic_loop_api.h"
#include "util.h"

template <class... Userdata>
IOEvent<Userdata...>::IOEvent(asio::io_service::strand& strand_, asio::io_service& io_service,
                              int fd, Userdata... userdata_, callback_t callback_)
        : strand(strand_), socket(io_service), this_ptr(this), userdata(userdata_...),
          callback(callback_), destroy_callback(nullptr), dead(false) {
    // We have to duplicate file descriptor because socket takes ownership of it
    // and we don't want it to do so.
    int new_fd = dup(fd);
    if (new_fd == -1) {
        throw GenericLoopApiException("Couldn't duplicate file descriptor" +
                                      std::string(strerror(errno)));
    }
    socket.assign(asio::generic::stream_protocol::socket::protocol_type(0, 0), new_fd);
}

template <class... Userdata>
IOEvent<Userdata...>::~IOEvent() {
    if (destroy_callback) {
        call(destroy_callback, std::tuple_cat(std::make_tuple(this), userdata));
    }
}

template <class... Userdata>
void IOEvent<Userdata...>::set_destroy_callback(destroy_callback_t destroy_callback_) {
    assert(!dead && strand.running_in_this_thread());
    destroy_callback = destroy_callback_;
}

template <class... Userdata>
void IOEvent<Userdata...>::free() {
    assert(!dead && strand.running_in_this_thread());
    dead = true;
    socket.cancel();
    this_ptr.reset();
}

template <class... Userdata>
void IOEvent<Userdata...>::update(IOEventFlags flags) {
    assert(!dead && strand.running_in_this_thread());
    socket.cancel();
    start_monitor(flags);
    updated = true;
}

template <class... Userdata>
IOEventFlags IOEvent<Userdata...>::get_flags() const {
    assert(!dead && strand.running_in_this_thread());
    return current_flags;
}

template <class... Userdata>
void IOEvent<Userdata...>::event_handler(IOEventFlags flag, const asio::error_code& error) {
    if (error == asio::error::operation_aborted) return;
    if (dead) return;

    current_flags = flag;
    if (error && error != asio::error::eof) {
        current_flags |= IOEventFlags::ERROR;
    }

    updated = false;
    call(callback,
         std::tuple_cat(std::make_tuple(this, socket.native_handle(), current_flags), userdata));
    current_flags = IOEventFlags::NONE;
    if (!updated && !dead) {
        start_monitor(flag);
    }
}

template <class... Userdata>
void IOEvent<Userdata...>::start_monitor(IOEventFlags flags) {
    assert(!dead);
    if ((flags & IOEventFlags::INPUT) != IOEventFlags::NONE) {
        this_ptr.get();
        socket.async_read_some(asio::null_buffers(),
                               strand.wrap([ this_ptr_copy = this_ptr, this ](
                                       const asio::error_code& error, std::size_t) {
                                   event_handler(IOEventFlags::INPUT, error);
                               }));
    }
    if ((flags & IOEventFlags::OUTPUT) != IOEventFlags::NONE) {
        socket.async_write_some(asio::null_buffers(),
                                strand.wrap([ this_ptr_copy = this_ptr, this ](
                                        const asio::error_code& error, std::size_t) {
                                    event_handler(IOEventFlags::OUTPUT, error);
                                }));
    }
}

template <class... Userdata>
TimerEvent<Userdata...>::TimerEvent(asio::io_service::strand& strand_, asio::io_service& io_service,
                                    Userdata... userdata_, callback_t callback_)
        : strand(strand_), timer(io_service), this_ptr(this), userdata(userdata_...),
          callback(callback_), destroy_callback(nullptr), dead(false) {}

template <class... Userdata>
TimerEvent<Userdata...>::~TimerEvent() {
    if (destroy_callback) {
        call(destroy_callback, std::tuple_cat(std::make_tuple(this), userdata));
    }
}

template <class... Userdata>
void TimerEvent<Userdata...>::set_destroy_callback(destroy_callback_t destroy_callback_) {
    assert(!dead && strand.running_in_this_thread());
    destroy_callback = destroy_callback_;
}

template <class... Userdata>
void TimerEvent<Userdata...>::free() {
    assert(!dead && strand.running_in_this_thread());
    dead = true;
    timer.cancel();
    this_ptr.reset();
}

template <class... Userdata>
void TimerEvent<Userdata...>::update(const struct timeval* tv) {
    assert(!dead && strand.running_in_this_thread());
    if (!tv) {
        timer.cancel();
        return;
    }

    deadline = *tv;
    timer.expires_at(asio::steady_timer::time_point(std::chrono::seconds(deadline.tv_sec) +
                                                    std::chrono::microseconds(deadline.tv_usec)));

    timer.async_wait(strand.wrap([ this_ptr_copy = this_ptr, this ](const asio::error_code& error) {
        expired_handler(error);
    }));
}

template <class... Userdata>
void TimerEvent<Userdata...>::expired_handler(const asio::error_code& error) {
    if (error) return;
    if (dead) return;
    call(callback,
         std::tuple_cat(std::make_tuple(this, const_cast<const timeval*>(&deadline)), userdata));
}

template <class... Userdata>
DeferedEvent<Userdata...>::DeferedEvent(asio::io_service::strand& strand_, Userdata... userdata_,
                                        callback_t callback_)
        : strand(strand_), this_ptr(this), userdata(userdata_...), callback(callback_),
          destroy_callback(nullptr), dead(false), running(false), posted(false) {}

template <class... Userdata>
DeferedEvent<Userdata...>::~DeferedEvent() {
    if (destroy_callback) {
        call(destroy_callback, std::tuple_cat(std::make_tuple(this), userdata));
    }
}

template <class... Userdata>
void DeferedEvent<Userdata...>::set_destroy_callback(destroy_callback_t destroy_callback_) {
    assert(!dead && strand.running_in_this_thread());
    destroy_callback = destroy_callback_;
}

template <class... Userdata>
void DeferedEvent<Userdata...>::free() {
    assert(!dead && strand.running_in_this_thread());
    dead = true;
    running = false;
    this_ptr.reset();
}

template <class... Userdata>
void DeferedEvent<Userdata...>::update(bool enable) {
    assert(strand.running_in_this_thread());
    if (enable) {
        running = true;
        if (!posted) {
            posted = true;
            strand.post([ copy_this_ptr = this_ptr, this ] { defered_handler(); });
        }
    } else {
        running = false;
    }
}

template <class... Userdata>
void DeferedEvent<Userdata...>::defered_handler() {
    posted = false;
    if (running) {
        call(callback, std::tuple_cat(std::make_tuple(this), userdata));
        update(running);
    }
}
