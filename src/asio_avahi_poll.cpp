#include <cassert>
#include <chrono>
#include <functional>

#include <avahi-common/simple-watch.h>

#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include "asio_avahi_poll.h"

struct AvahiWatch {
  public:
    AvahiWatch(const AvahiWatch&) = delete;

    AvahiWatch(boost::asio::io_service::strand& strand_, boost::asio::io_service& io_service,
               int fd, AvahiWatchCallback callback_, void* userdata_)
            : strand(strand_), socket(io_service), callback(callback_), userdata(userdata_),
              dead(false), in_callback(false) {
        socket.assign(boost::asio::generic::stream_protocol::socket::protocol_type(0, 0), fd);
    }

    ~AvahiWatch() {
        socket.cancel();
    }

    void update(AvahiWatchEvent events) {
        assert(!dead);
        socket.cancel();
        start_monitor(events);
    }

    AvahiWatchEvent get_events() const {
        assert(!dead);
        return event_happened;
    }

    void free() {
        assert(!dead);
        dead = true;
        if (!in_callback) {
            delete this;
        }
    }

    boost::asio::io_service::strand& strand;

  private:
    void event_handler(AvahiWatchEvent event, const boost::system::error_code& error, std::size_t) {
        if (dead) return;
        start_monitor(event);

        event_happened = event;
        if (error && error != boost::asio::error::eof) {
            event_happened = static_cast<AvahiWatchEvent>(static_cast<int>(event_happened) |
                                                          static_cast<int>(AVAHI_WATCH_ERR));
        }
        in_callback = true;
        callback(this, socket.native_handle(), event_happened, userdata);
        in_callback = false;
        if (dead) {
            delete this;
        } else {
            event_happened = static_cast<AvahiWatchEvent>(0);
        }
    }

    void start_monitor(AvahiWatchEvent events) {
        assert(!dead);
        using namespace std::placeholders;
        if (events & AVAHI_WATCH_IN) {
            socket.async_read_some(boost::asio::null_buffers(),
                                   strand.wrap(std::bind(&AvahiWatch::event_handler, this,
                                                         AVAHI_WATCH_IN, _1, _2)));
        }
        if (events & AVAHI_WATCH_OUT) {
            socket.async_write_some(boost::asio::null_buffers(),
                                    strand.wrap(std::bind(&AvahiWatch::event_handler, this,
                                                          AVAHI_WATCH_OUT, _1, _2)));
        }
    }

    boost::asio::generic::stream_protocol::socket socket;
    AvahiWatchEvent event_happened;
    AvahiWatchCallback callback;
    void* userdata;
    bool dead, in_callback;
};

struct AvahiTimeout {
  public:
    AvahiTimeout(const AvahiTimeout&) = delete;

    AvahiTimeout(boost::asio::io_service::strand& strand_, boost::asio::io_service& io_service,
                 AvahiTimeoutCallback callback_, void* userdata_)
            : strand(strand_), timer(io_service), callback(callback_), userdata(userdata_),
              dead(false) {}

    ~AvahiTimeout() {
        timer.cancel();
    }

    void update(const struct timeval* tv) {
        assert(!dead);
        if (!tv) {
            timer.cancel();
            return;
        }

        timer.expires_at(boost::asio::steady_timer::time_point(
                std::chrono::seconds(tv->tv_sec) + std::chrono::microseconds(tv->tv_usec)));
        timer.async_wait(
                strand.wrap(std::bind(&AvahiTimeout::expired, this, std::placeholders::_1)));
    }

    void free() {
        assert(!dead);
        dead = true;
        delete this;
    }

    boost::asio::io_service::strand& strand;

  private:
    void expired(const boost::system::error_code& error) {
        if (dead || error) return;
        callback(this, userdata);
    }

    boost::asio::steady_timer timer;
    AvahiTimeoutCallback callback;
    void* userdata;
    bool dead;
};

AvahiWatch* AsioAvahiPoll::watch_new(const AvahiPoll* api, int fd, AvahiWatchEvent event,
                                     AvahiWatchCallback callback, void* userdata) {
    auto poll = static_cast<AsioAvahiPoll*>(api->userdata);
    assert(poll->strand.running_in_this_thread());
    auto watch = new AvahiWatch(poll->strand, poll->io_service, fd, callback, userdata);
    watch->update(event);
    return watch;
}

void AsioAvahiPoll::watch_update(AvahiWatch* w, AvahiWatchEvent event) {
    assert(w->strand.running_in_this_thread());
    w->update(event);
}

AvahiWatchEvent AsioAvahiPoll::watch_get_events(AvahiWatch* w) {
    assert(w->strand.running_in_this_thread());
    return w->get_events();
}

void AsioAvahiPoll::watch_free(AvahiWatch* w) {
    assert(w->strand.running_in_this_thread());
    w->free();
}

AvahiTimeout* AsioAvahiPoll::timeout_new(const AvahiPoll* api, const struct timeval* tv,
                                         AvahiTimeoutCallback callback, void* userdata) {
    auto poll = static_cast<AsioAvahiPoll*>(api->userdata);
    assert(poll->strand.running_in_this_thread());
    auto timer = new AvahiTimeout(poll->strand, poll->io_service, callback, userdata);
    timer->update(tv);
    return timer;
}

void AsioAvahiPoll::timeout_update(AvahiTimeout* t, const struct timeval* tv) {
    assert(t->strand.running_in_this_thread());
    t->update(tv);
}

void AsioAvahiPoll::timeout_free(AvahiTimeout* t) {
    assert(t->strand.running_in_this_thread());
    t->free();
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
