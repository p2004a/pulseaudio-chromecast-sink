#pragma once

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>

#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

enum class IOEventFlags : unsigned char {
    NONE = 0,
    INPUT = 1 << 0,
    OUTPUT = 1 << 1,
    HANGUP = 1 << 2,
    ERROR = 1 << 3,
};

inline IOEventFlags operator|(IOEventFlags a, IOEventFlags b) {
    return static_cast<IOEventFlags>(static_cast<std::underlying_type_t<IOEventFlags>>(a) |
                                     static_cast<std::underlying_type_t<IOEventFlags>>(b));
}

inline IOEventFlags& operator|=(IOEventFlags& a, IOEventFlags b) {
    a = a | b;
    return a;
}

inline IOEventFlags operator&(IOEventFlags a, IOEventFlags b) {
    return static_cast<IOEventFlags>(static_cast<std::underlying_type_t<IOEventFlags>>(a) &
                                     static_cast<std::underlying_type_t<IOEventFlags>>(b));
}

template <class... Userdata>
class IOEvent {
  public:
    typedef std::function<void(IOEvent*, int, IOEventFlags, Userdata...)> callback_t;
    typedef std::function<void(IOEvent*, Userdata...)> destroy_callback_t;

    IOEvent(const IOEvent&) = delete;
    IOEvent(boost::asio::io_service::strand& strand_, boost::asio::io_service& io_service, int fd,
            Userdata... userdata_, callback_t callback_);
    ~IOEvent();

    void set_destroy_callback(destroy_callback_t destroy_callback_);
    void free();
    void update(IOEventFlags flags);
    IOEventFlags get_flags() const;

  private:
    void event_handler(IOEventFlags flag, const boost::system::error_code& error);
    void start_monitor(IOEventFlags flags);

    boost::asio::io_service::strand& strand;
    boost::asio::generic::stream_protocol::socket socket;
    std::shared_ptr<IOEvent> this_ptr;
    std::tuple<Userdata...> userdata;
    callback_t callback;
    destroy_callback_t destroy_callback;
    bool dead;
    IOEventFlags current_flags;
};

template <class... Userdata>
class TimerEvent {
  public:
    typedef std::function<void(TimerEvent*, const struct timeval*, Userdata...)> callback_t;
    typedef std::function<void(TimerEvent*, Userdata...)> destroy_callback_t;

    TimerEvent(const TimerEvent&) = delete;
    TimerEvent(boost::asio::io_service::strand& strand_, boost::asio::io_service& io_service,
               Userdata... userdata_, callback_t callback_);
    ~TimerEvent();

    void set_destroy_callback(destroy_callback_t destroy_callback_);
    void free();
    void update(const struct timeval* tv);

  private:
    void expired_handler(const boost::system::error_code& error);

    boost::asio::io_service::strand& strand;
    boost::asio::steady_timer timer;
    struct timeval deadline;
    std::shared_ptr<TimerEvent> this_ptr;
    std::tuple<Userdata...> userdata;
    callback_t callback;
    destroy_callback_t destroy_callback;
    bool dead;
};

template <class... Userdata>
class DeferedEvent {
  public:
    typedef std::function<void(DeferedEvent*, Userdata...)> callback_t;
    typedef std::function<void(DeferedEvent*, Userdata...)> destroy_callback_t;

    DeferedEvent(const DeferedEvent&) = delete;
    DeferedEvent(boost::asio::io_service::strand& strand_, Userdata... userdata_,
                 callback_t callback_);
    ~DeferedEvent();

    void set_destroy_callback(destroy_callback_t destroy_callback_);
    void free();
    void update(bool enable);

  private:
    void defered_handler();

    boost::asio::io_service::strand& strand;
    std::shared_ptr<DeferedEvent> this_ptr;
    std::tuple<Userdata...> userdata;
    callback_t callback;
    destroy_callback_t destroy_callback;
    bool dead, running, posted;
};

#include "generic_loop_api_impl.h"
