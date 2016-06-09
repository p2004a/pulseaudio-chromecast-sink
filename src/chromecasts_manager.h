/* chromecasts_manager.h -- This file is part of pulseaudio-chromecast-sink
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

#include <memory>
#include <unordered_map>

#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>

#include "audio_sinks_manager.h"
#include "chromecast_channel.h"
#include "chromecast_connection.h"
#include "chromecast_finder.h"
#include "websocket_broadcaster.h"

class ChromecastsManagerException : public std::runtime_error {
  public:
    ChromecastsManagerException(std::string message) : std::runtime_error(message) {}
};

class ChromecastsManager;

class Chromecast : public std::enable_shared_from_this<Chromecast> {
  private:
    struct private_tag {};

  public:
    Chromecast(const Chromecast&) = delete;

    void update_info(ChromecastFinder::ChromecastInfo info_);

    void set_message_handler(WebsocketBroadcaster::MessageHandler handler);

    Chromecast(ChromecastsManager& manager_, ChromecastFinder::ChromecastInfo info_, private_tag);

    static std::shared_ptr<Chromecast> create(ChromecastsManager& manager_,
                                              ChromecastFinder::ChromecastInfo info_);

  private:
    void init();

    template <class F>
    auto weak_wrap(F&& f) {
        return strand.wrap(wrap_weak_ptr(std::forward<F>(f), this));
    }

    void volume_callback(double left, double right, bool muted);
    void activation_callback(bool activate);
    void connection_error_handler(std::string message);
    void connection_connected_handler(bool connected);
    void connection_message_handler(const cast_channel::CastMessage& message);
    void handle_app_load(nlohmann::json);

    ChromecastsManager& manager;
    std::shared_ptr<AudioSink> sink;
    ChromecastFinder::ChromecastInfo info;
    std::shared_ptr<ChromecastConnection> connection;
    boost::asio::io_service::strand strand;
    std::mutex message_handler_mu;
    WebsocketBroadcaster::MessageHandler message_handler;
    bool activated;
    std::shared_ptr<MainChromecastChannel> main_channel;
    std::shared_ptr<AppChromecastChannel> app_channel;
    std::string transport_id, session_id;
};

class ChromecastsManager {
  public:
    typedef std::function<void(const std::string&)> ErrorHandler;

    ChromecastsManager(const ChromecastsManager&) = delete;

    ChromecastsManager(boost::asio::io_service& io_service_, const char* logger_name = "default");

    void set_error_handler(ErrorHandler error_handler_);
    void stop();

  private:
    void finder_callback(ChromecastFinder::UpdateType type, ChromecastFinder::ChromecastInfo info);
    void websocket_subscribe_callback(WebsocketBroadcaster::MessageHandler, std::string);
    void propagate_error(const std::string& message);

    std::shared_ptr<spdlog::logger> logger;
    boost::asio::io_service& io_service;
    boost::asio::io_service::strand chromecasts_strand;
    std::unordered_map<std::string, std::shared_ptr<Chromecast>> chromecasts;
    AudioSinksManager sinks_manager;
    ChromecastFinder finder;
    WebsocketBroadcaster broadcaster;
    ErrorHandler error_handler;

    friend class Chromecast;
};
