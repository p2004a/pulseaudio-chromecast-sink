/* chromecast_channel.h -- This file is part of pulseaudio-chromecast-sink
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
#include <memory>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include <json.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include "proto/cast_channel.pb.h"

#include "util.h"

constexpr const char* CHCHANNS_CONNECTION = "urn:x-cast:com.google.cast.tp.connection";
constexpr const char* CHCHANNS_HEARTBEAT = "urn:x-cast:com.google.cast.tp.heartbeat";
constexpr const char* CHCHANNS_RECEIVER = "urn:x-cast:com.google.cast.receiver";
constexpr const char* CHCHANNS_STREAM_APP = "urn:x-cast:com.p2004a.chromecast-receiver.wsapp";

/*
 * This class provides basic implementation of Chromecast virtual connection.
 */
template <class T>
class BaseChromecastChannel : public std::enable_shared_from_this<T> {
  protected:
    struct private_tag {};

  public:
    typedef std::function<void(cast_channel::CastMessage)> MessageFunc;

    BaseChromecastChannel(const BaseChromecastChannel&) = delete;

    BaseChromecastChannel(boost::asio::io_service& io_service, std::string name_,
                          std::string destination_, MessageFunc send_func_, const char* logger_name,
                          private_tag);

    static std::shared_ptr<T> create(boost::asio::io_service& io_service, std::string name_,
                                     std::string destination_, MessageFunc send_func_,
                                     const char* logger_name = "default");

    void dispatch_message(cast_channel::CastMessage message);

  protected:
    typedef std::function<void(nlohmann::json)> ParsedMessageFunc;

    void register_namespace_callback(std::string ns, ParsedMessageFunc func);
    void send_message(std::string ns, nlohmann::json msg);

    std::shared_ptr<spdlog::logger> logger;

    template <class F>
    auto weak_wrap(F&& f) {
        return strand.wrap(wrap_weak_ptr(std::forward<F>(f), this));
    }

    template <class F>
    void weak_dispatch(F&& f) {
        strand.dispatch(wrap_weak_ptr(std::forward<F>(f), this));
    }

  private:
    void real_message_dispatch(const cast_channel::CastMessage& message);

    std::unordered_map<std::string, ParsedMessageFunc> namespace_handlers;
    boost::asio::io_service::strand strand;
    std::string name, destination;
    MessageFunc send_func;
};

/*
 * This class implements two namespaces: urn:x-cast:com.google.cast.tp.connection and
 * urn:x-cast:com.google.cast.tp.heartbeat
 */
template <class T>
class BasicChromecastChannel : public BaseChromecastChannel<T> {
  public:
    BasicChromecastChannel(boost::asio::io_service& io_service, std::string name_,
                           std::string destination_,
                           typename BaseChromecastChannel<T>::MessageFunc send_func_,
                           const char* logger_name, typename BaseChromecastChannel<T>::private_tag);

    void start();

  private:
    void handle_connect_channel(nlohmann::json msg);
    void handle_heartbeat_channel(nlohmann::json msg);
    void timer_expired_callback(const boost::system::error_code& error);

    boost::asio::steady_timer timer;
};

/*
 * This class is implementation of urn:x-cast:com.google.cast.receiver namespace.
 */
class MainChromecastChannel : public BasicChromecastChannel<MainChromecastChannel> {
  public:
    typedef std::function<void(nlohmann::json msg)> StatusCb;

    MainChromecastChannel(boost::asio::io_service& io_service, std::string name_,
                          std::string destination_, MessageFunc send_func_, const char* logger_name,
                          private_tag);

    void load_app(std::string app_id, StatusCb loaded_callback);
    void get_status(StatusCb status_callback);
    void stop_app(std::string session_id, StatusCb stopped_callback);

  private:
    void handle_receiver_channel(nlohmann::json msg);

    int curr_request_id;
    std::unordered_map<int, StatusCb> pending_requests;
};

class AppChromecastChannel : public BasicChromecastChannel<AppChromecastChannel> {
  public:
    struct Result {
        Result(nlohmann::json data_) : ok(true), data(data_) {}
        Result(std::string message_) : ok(false), message(message_) {}

        bool ok;
        nlohmann::json data;
        std::string message;
    };

    typedef std::function<void(Result)> ResultCb;

    AppChromecastChannel(boost::asio::io_service& io_service, std::string name_,
                         std::string destination_, MessageFunc send_func_, const char* logger_name,
                         private_tag);

    template <class It>
    void start_stream(It begin, It end, std::string device_name, ResultCb);

  private:
    void handle_app_channel(nlohmann::json msg);

    int curr_request_id;
    std::unordered_map<int, ResultCb> pending_requests;
};

#include "chromecast_channel_impl.h"
