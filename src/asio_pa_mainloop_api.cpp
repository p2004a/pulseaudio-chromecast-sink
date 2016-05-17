#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <functional>

#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include "asio_pa_mainloop_api.h"

struct pa_io_event {
  public:
    pa_io_event(const pa_io_event&) = delete;

    pa_io_event(AsioPulseAudioMainloop* mainloop_, int fd, pa_io_event_cb_t cb, void* userdata_)
            : mainloop(mainloop_), socket(mainloop->io_service), dead(false), in_async(0),
              callback(cb), userdata(userdata_), destroy_callback(nullptr) {
        // We have to duplicate file descriptor because socket takes ownership of it
        // and we don't want it to do so.
        int new_fd = dup(fd);
        if (new_fd == -1) {
            throw std::runtime_error("Couldn't duplicate file descriptor" +
                                     std::string(strerror(errno)));
        }
        socket.assign(boost::asio::generic::stream_protocol::socket::protocol_type(0, 0), new_fd);
    }

    ~pa_io_event() {
        if (destroy_callback) {
            destroy_callback(&mainloop->api, this, userdata);
        }
    }

    void enable(pa_io_event_flags_t events) {
        assert(!dead);
        socket.cancel();
        start_monitor(events);
    }

    void free() {
        assert(!dead);
        dead = true;
        socket.cancel();
        if (in_async == 0) {
            delete this;
        }
    }

    void set_destroy(pa_io_event_destroy_cb_t cb) {
        assert(!dead);
        destroy_callback = cb;
    }

    AsioPulseAudioMainloop* mainloop;

  private:
    void event_handler(pa_io_event_flags_t event, const boost::system::error_code& error,
                       std::size_t) {
        --in_async;
        if (dead && in_async == 0) {
            delete this;
            return;
        }

        if (error == boost::asio::error::operation_aborted) return;
        start_monitor(event);

        if (error && error != boost::asio::error::eof) {
            event = static_cast<pa_io_event_flags_t>(static_cast<int>(event) |
                                                     static_cast<int>(PA_IO_EVENT_ERROR));
        }
        callback(&mainloop->api, this, socket.native_handle(), event, userdata);
    }

    void start_monitor(pa_io_event_flags_t events) {
        assert(!dead);
        using namespace std::placeholders;
        if (events & PA_IO_EVENT_INPUT) {
            ++in_async;
            socket.async_read_some(
                    boost::asio::null_buffers(),
                    mainloop->strand.wrap(std::bind(&pa_io_event::event_handler, this,
                                                    PA_IO_EVENT_INPUT, _1, _2)));
        }
        if (events & PA_IO_EVENT_OUTPUT) {
            ++in_async;
            socket.async_write_some(
                    boost::asio::null_buffers(),
                    mainloop->strand.wrap(std::bind(&pa_io_event::event_handler, this,
                                                    PA_IO_EVENT_OUTPUT, _1, _2)));
        }
    }

    boost::asio::generic::stream_protocol::socket socket;
    bool dead;
    int in_async;
    pa_io_event_cb_t callback;
    void* userdata;
    pa_io_event_destroy_cb_t destroy_callback;
};

struct pa_time_event {
  public:
    pa_time_event(const pa_time_event&) = delete;

    pa_time_event(AsioPulseAudioMainloop* mainloop_, pa_time_event_cb_t cb, void* userdata_)
            : mainloop(mainloop_), timer(mainloop->io_service), dead(false), in_async_wait(0),
              callback(cb), userdata(userdata_), destroy_callback(nullptr) {}

    ~pa_time_event() {
        if (destroy_callback) {
            destroy_callback(&mainloop->api, this, userdata);
        }
    }

    void restart(const struct timeval* tv) {
        assert(!dead);
        if (!tv) {
            timer.cancel();
            return;
        }

        deadline = *tv;
        timer.expires_at(
                boost::asio::steady_timer::time_point(std::chrono::seconds(deadline.tv_sec) +
                                                      std::chrono::microseconds(deadline.tv_usec)));
        ++in_async_wait;
        timer.async_wait(mainloop->strand.wrap(
                std::bind(&pa_time_event::expired, this, std::placeholders::_1)));
    }

    void free() {
        assert(!dead);
        dead = true;
        timer.cancel();
        if (in_async_wait == 0) {
            delete this;
        }
    }

    void set_destroy(pa_time_event_destroy_cb_t cb) {
        assert(!dead);
        destroy_callback = cb;
    }

    AsioPulseAudioMainloop* mainloop;

  private:
    void expired(const boost::system::error_code& error) {
        --in_async_wait;
        if (dead && in_async_wait == 0) {
            delete this;
            return;
        }

        if (error) return;
        callback(&mainloop->api, this, &deadline, userdata);
    }

    boost::asio::steady_timer timer;
    struct timeval deadline;
    bool dead;
    int in_async_wait;
    pa_time_event_cb_t callback;
    void* userdata;
    pa_time_event_destroy_cb_t destroy_callback;
};

struct pa_defer_event {
  public:
    pa_defer_event(const pa_defer_event&) = delete;

    pa_defer_event(AsioPulseAudioMainloop* mainloop_, pa_defer_event_cb_t cb, void* userdata_)
            : mainloop(mainloop_), dead(false), posted(false), callback(cb), userdata(userdata_),
              destroy_callback(nullptr) {
        enable(1);
    }

    ~pa_defer_event() {
        if (destroy_callback) {
            destroy_callback(&mainloop->api, this, userdata);
        }
    }

    void enable(int b) {
        assert(!dead);
        if (b) {
            running = true;
            if (!posted) {
                posted = true;
                mainloop->strand.post(std::bind(&pa_defer_event::run, this));
            }
        } else {
            running = false;
        }
    }

    void free() {
        assert(!dead);
        dead = true;
        if (!posted) {
            delete this;
        }
    }

    void set_destroy(pa_defer_event_destroy_cb_t cb) {
        assert(!dead);
        destroy_callback = cb;
    }

    AsioPulseAudioMainloop* mainloop;

  private:
    void run() {
        if (dead) {
            delete this;
            return;
        }
        if (!running) {
            posted = false;
            return;
        }
        callback(&mainloop->api, this, userdata);
        mainloop->strand.post(std::bind(&pa_defer_event::run, this));
    }

    bool dead, running, posted;
    pa_defer_event_cb_t callback;
    void* userdata;
    pa_defer_event_destroy_cb_t destroy_callback;
};

pa_io_event* AsioPulseAudioMainloop::io_new(pa_mainloop_api* a, int fd, pa_io_event_flags_t events,
                                            pa_io_event_cb_t cb, void* userdata) {
    auto api = static_cast<AsioPulseAudioMainloop*>(a->userdata);
    assert(api->strand.running_in_this_thread());
    auto io_event = new pa_io_event(api, fd, cb, userdata);
    io_event->enable(events);
    return io_event;
}

void AsioPulseAudioMainloop::io_enable(pa_io_event* e, pa_io_event_flags_t events) {
    assert(e->mainloop->strand.running_in_this_thread());
    e->enable(events);
}

void AsioPulseAudioMainloop::io_free(pa_io_event* e) {
    assert(e->mainloop->strand.running_in_this_thread());
    e->free();
}

void AsioPulseAudioMainloop::io_set_destroy(pa_io_event* e, pa_io_event_destroy_cb_t cb) {
    assert(e->mainloop->strand.running_in_this_thread());
    e->set_destroy(cb);
}

pa_time_event* AsioPulseAudioMainloop::time_new(pa_mainloop_api* a, const struct timeval* tv,
                                                pa_time_event_cb_t cb, void* userdata) {
    auto api = static_cast<AsioPulseAudioMainloop*>(a->userdata);
    assert(api->strand.running_in_this_thread());
    auto time_event = new pa_time_event(api, cb, userdata);
    time_event->restart(tv);
    return time_event;
}

void AsioPulseAudioMainloop::time_restart(pa_time_event* e, const struct timeval* tv) {
    assert(e->mainloop->strand.running_in_this_thread());
    e->restart(tv);
}

void AsioPulseAudioMainloop::time_free(pa_time_event* e) {
    assert(e->mainloop->strand.running_in_this_thread());
    e->free();
}

void AsioPulseAudioMainloop::time_set_destroy(pa_time_event* e, pa_time_event_destroy_cb_t cb) {
    assert(e->mainloop->strand.running_in_this_thread());
    e->set_destroy(cb);
}

pa_defer_event* AsioPulseAudioMainloop::defer_new(pa_mainloop_api* a, pa_defer_event_cb_t cb,
                                                  void* userdata) {
    auto api = static_cast<AsioPulseAudioMainloop*>(a->userdata);
    assert(api->strand.running_in_this_thread());
    auto defer_event = new pa_defer_event(api, cb, userdata);
    return defer_event;
}

void AsioPulseAudioMainloop::defer_enable(pa_defer_event* e, int b) {
    assert(e->mainloop->strand.running_in_this_thread());
    e->enable(b);
}

void AsioPulseAudioMainloop::defer_free(pa_defer_event* e) {
    assert(e->mainloop->strand.running_in_this_thread());
    e->free();
}

void AsioPulseAudioMainloop::defer_set_destroy(pa_defer_event* e, pa_defer_event_destroy_cb_t cb) {
    assert(e->mainloop->strand.running_in_this_thread());
    e->set_destroy(cb);
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
