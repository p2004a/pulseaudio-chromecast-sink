/* websocket_broadcaster.h -- This file is part of pulseaudio-chromecast-sink
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

#include <functional>
#include <set>

#include <spdlog/spdlog.h>

#include <boost/asio/io_service.hpp>

#include <boost/asio/strand.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "audio_sinks_manager.h"

#pragma once

class WebsocketBroadcaster {
  public:
    struct MessageHandler {
        websocketpp::connection_hdl hdl;
        WebsocketBroadcaster* this_ptr = nullptr;
    };
    typedef std::function<void(MessageHandler, std::string)> SubscribeCallback;

    WebsocketBroadcaster(const WebsocketBroadcaster&) = delete;

    WebsocketBroadcaster(boost::asio::io_service& io_service_,
                         SubscribeCallback subscribe_callback_,
                         const char* logger_name = "default");

    void stop();

    static void send_samples(MessageHandler hdl, const AudioSample* samples, size_t num);

  private:
    // TODO: create own configuration with own logger based on spdlog
    typedef websocketpp::server<websocketpp::config::asio> WebsocketServer;

    void on_message(websocketpp::connection_hdl hdl, WebsocketServer::message_ptr);
    void on_open(websocketpp::connection_hdl hdl);
    void on_close(websocketpp::connection_hdl hdl);
    void on_socket_init(websocketpp::connection_hdl hdl, boost::asio::ip::tcp::socket& s);

    std::shared_ptr<spdlog::logger> logger;
    boost::asio::io_service& io_service;
    boost::asio::io_service::strand connections_strand;
    std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> connections;
    WebsocketServer ws_server;
    SubscribeCallback subscribe_callback;
};
