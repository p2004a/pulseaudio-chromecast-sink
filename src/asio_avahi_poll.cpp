/* asio_avahi_poll.cpp -- This file is part of pulseaudio-chromecast-sink
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

#include <avahi-common/simple-watch.h>

#include <boost/asio/io_service.hpp>

#include "asio_avahi_poll.h"
#include "generic_loop_api.h"

typedef IOEvent<void*> AvahiIOEvent;
typedef TimerEvent<void*> AvahiTimerEvent;
typedef DeferedEvent<void*> AvahiDeferedEvent;

IOEventFlags map_io_flags_from_avahi(AvahiWatchEvent pa_flags) {
    IOEventFlags flags = IOEventFlags::NONE;
    if (pa_flags & AVAHI_WATCH_ERR) flags |= IOEventFlags::ERROR;
    if (pa_flags & AVAHI_WATCH_IN) flags |= IOEventFlags::INPUT;
    if (pa_flags & AVAHI_WATCH_OUT) flags |= IOEventFlags::OUTPUT;
    if (pa_flags & AVAHI_WATCH_HUP) flags |= IOEventFlags::HANGUP;
    return flags;
}

AvahiWatchEvent map_io_flags_to_avahi(IOEventFlags flags) {
    AvahiWatchEvent event = static_cast<AvahiWatchEvent>(0);
    if ((flags & IOEventFlags::ERROR) != IOEventFlags::NONE)
        event = static_cast<AvahiWatchEvent>(event | AVAHI_WATCH_ERR);
    if ((flags & IOEventFlags::INPUT) != IOEventFlags::NONE)
        event = static_cast<AvahiWatchEvent>(event | AVAHI_WATCH_IN);
    if ((flags & IOEventFlags::OUTPUT) != IOEventFlags::NONE)
        event = static_cast<AvahiWatchEvent>(event | AVAHI_WATCH_OUT);
    if ((flags & IOEventFlags::HANGUP) != IOEventFlags::NONE)
        event = static_cast<AvahiWatchEvent>(event | AVAHI_WATCH_HUP);
    return event;
}

AvahiWatch* AsioAvahiPoll::watch_new(const AvahiPoll* api, int fd, AvahiWatchEvent event,
                                     AvahiWatchCallback callback, void* userdata) {
    auto poll = static_cast<AsioAvahiPoll*>(api->userdata);
    assert(poll->strand.running_in_this_thread());
    auto watch = new AvahiIOEvent(
            poll->strand, poll->io_service, fd, userdata,
            [callback](AvahiIOEvent* event_, int fd_, IOEventFlags flags, void* userdata_) {
                callback(reinterpret_cast<AvahiWatch*>(event_), fd_, map_io_flags_to_avahi(flags),
                         userdata_);
            });
    watch->update(map_io_flags_from_avahi(event));
    return reinterpret_cast<AvahiWatch*>(watch);
}

void AsioAvahiPoll::watch_update(AvahiWatch* w, AvahiWatchEvent event) {
    reinterpret_cast<AvahiIOEvent*>(w)->update(map_io_flags_from_avahi(event));
}

AvahiWatchEvent AsioAvahiPoll::watch_get_events(AvahiWatch* w) {
    return map_io_flags_to_avahi(reinterpret_cast<AvahiIOEvent*>(w)->get_flags());
}

void AsioAvahiPoll::watch_free(AvahiWatch* w) {
    reinterpret_cast<AvahiIOEvent*>(w)->free();
}

AvahiTimeout* AsioAvahiPoll::timeout_new(const AvahiPoll* api, const struct timeval* tv,
                                         AvahiTimeoutCallback callback, void* userdata) {
    auto poll = static_cast<AsioAvahiPoll*>(api->userdata);
    assert(poll->strand.running_in_this_thread());
    auto timer = new AvahiTimerEvent(
            poll->strand, poll->io_service, userdata,
            [callback](AvahiTimerEvent* event, const struct timeval*, void* userdata_) {
                callback(reinterpret_cast<AvahiTimeout*>(event), userdata_);
            });
    timer->update(tv);
    return reinterpret_cast<AvahiTimeout*>(timer);
}

void AsioAvahiPoll::timeout_update(AvahiTimeout* t, const struct timeval* tv) {
    reinterpret_cast<AvahiTimerEvent*>(t)->update(tv);
}

void AsioAvahiPoll::timeout_free(AvahiTimeout* t) {
    reinterpret_cast<AvahiTimerEvent*>(t)->free();
}

AsioAvahiPoll::AsioAvahiPoll(boost::asio::io_service& io_service_)
        : io_service(io_service_), strand(io_service) {
    avahi_poll.userdata = this;
    avahi_poll.watch_new = watch_new;
    avahi_poll.watch_update = watch_update;
    avahi_poll.watch_get_events = watch_get_events;
    avahi_poll.watch_free = watch_free;
    avahi_poll.timeout_new = timeout_new;
    avahi_poll.timeout_update = timeout_update;
    avahi_poll.timeout_free = timeout_free;
}
