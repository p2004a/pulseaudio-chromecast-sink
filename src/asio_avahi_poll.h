/* asio_avahi_poll.h -- This file is part of pulseaudio-chromecast-sink
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

#include <avahi-common/simple-watch.h>

#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>

class AsioAvahiPoll {
  public:
    AsioAvahiPoll(const AsioAvahiPoll&) = delete;
    AsioAvahiPoll(boost::asio::io_service& io_service_);

    const AvahiPoll* get_pool() const {
        return &avahi_poll;
    }

    boost::asio::io_service::strand& get_strand() {
        return strand;
    }

  private:
    static AvahiWatch* watch_new(const AvahiPoll* api, int fd, AvahiWatchEvent event,
                                 AvahiWatchCallback callback, void* userdata);
    static void watch_update(AvahiWatch* w, AvahiWatchEvent event);
    static AvahiWatchEvent watch_get_events(AvahiWatch* w);
    static void watch_free(AvahiWatch* w);
    static AvahiTimeout* timeout_new(const AvahiPoll* api, const struct timeval* tv,
                                     AvahiTimeoutCallback callback, void* userdata);
    static void timeout_update(AvahiTimeout* t, const struct timeval* tv);
    static void timeout_free(AvahiTimeout* t);

    boost::asio::io_service& io_service;
    boost::asio::io_service::strand strand;
    AvahiPoll avahi_poll;
};
