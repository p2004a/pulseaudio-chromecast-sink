/* websocket_broadcaster.cpp -- This file is part of pulseaudio-chromecast-sink
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
#include <string>

#include <spdlog/spdlog.h>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <json.hpp>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "websocket_broadcaster.h"

using json = nlohmann::json;

WebsocketBroadcaster::WebsocketBroadcaster(boost::asio::io_service& io_service_,
                                           const char* logger_name)
        : io_service(io_service_) {
    using namespace std::placeholders;

    logger = spdlog::get(logger_name);
    ws_server.init_asio(&io_service);

    ws_server.set_close_handler(std::bind(&WebsocketBroadcaster::on_close, this, _1));
    ws_server.set_open_handler(std::bind(&WebsocketBroadcaster::on_open, this, _1));
    ws_server.set_message_handler(std::bind(&WebsocketBroadcaster::on_message, this, _1, _2));
    ws_server.set_socket_init_handler(
            std::bind(&WebsocketBroadcaster::on_socket_init, this, _1, _2));

    ws_server.listen(0);
    ws_server.start_accept();

    boost::system::error_code error;
    auto local_endpoint = ws_server.get_local_endpoint(error);
    if (error) {
        logger->error("(WebsocketBroadcaster) Couldn't get listening socket: {}", error);
    } else {
        logger->info("(WebsocketBroadcaster) Connecting on port {}", local_endpoint.port());
    }
}

void WebsocketBroadcaster::on_message(websocketpp::connection_hdl hdl,
                                      WebsocketServer::message_ptr message) try {
    std::string payload = message->get_payload();
    logger->trace("(WebsocketBroadcaster) Got message: {}", payload);
    auto json_msg = json::parse(payload);
    std::string type = json_msg["type"];
    if (type == "SUBSCRIBE") {
        std::string chromecast_name = json_msg["name"];
        logger->trace("(WebsocketBroadcaster) Chromecast {} subscribed", chromecast_name);
    } else {
        logger->warn("(WebsocketBroadcaster) Unexpected message type: {}", type);
    }
} catch (std::invalid_argument) {
    logger->warn("(WebsocketBroadcaster) Failed to parse JSON message from connection");
} catch (std::domain_error) {
    logger->warn("(WebsocketBroadcaster) JSON message didn't have expected fields");
}

void WebsocketBroadcaster::stop() {
    ws_server.stop_listening();
    connections_strand.dispatch([this] {
        for (auto hdl : connections) {
            try {
                logger->trace("(WebsocketBroadcaster) stopping connection");
                ws_server.close(hdl, websocketpp::close::status::normal, "");
            } catch (websocketpp::lib::error_code ec) {
                logger->error("(WebsocketBroadcaster) closing connection failed: {}", ec);
            }
        }
    });
}

void WebsocketBroadcaster::on_open(websocketpp::connection_hdl hdl) {
    logger->trace("(WebsocketBroadcaster) New connection");
    connections_strand.dispatch([this, hdl] { connections.insert(hdl); });
}

void WebsocketBroadcaster::on_close(websocketpp::connection_hdl hdl) {
    logger->trace("(WebsocketBroadcaster) Closed connection");
    connections_strand.dispatch([this, hdl] { connections.erase(hdl); });
}

void WebsocketBroadcaster::on_socket_init(websocketpp::connection_hdl /*hdl*/,
                                          boost::asio::ip::tcp::socket& s) {
    logger->trace("(WebsocketBroadcaster) Setting new socket tcp::no_delay option");
    boost::asio::ip::tcp::no_delay option(true);
    s.set_option(option);
}
