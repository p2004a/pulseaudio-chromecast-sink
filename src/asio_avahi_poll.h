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
