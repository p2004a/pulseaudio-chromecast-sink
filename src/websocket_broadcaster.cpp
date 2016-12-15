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

#include <cassert>
#include <functional>
#include <string>

#include <spdlog/spdlog.h>

#include <asio/io_service.hpp>
#include <asio/ip/tcp.hpp>

#include <json.hpp>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "websocket_broadcaster.h"

using json = nlohmann::json;

WebsocketBroadcaster::WebsocketBroadcaster(asio::io_service& io_service_, const char* logger_name)
        : io_service(io_service_), connections_strand(io_service) {
    using namespace std::placeholders;

    logger = spdlog::get(logger_name);
    ws_server.init_asio(&io_service);
    ws_server.clear_access_channels(websocketpp::log::alevel::all);
    ws_server.clear_error_channels(websocketpp::log::elevel::all);

    ws_server.set_close_handler(std::bind(&WebsocketBroadcaster::on_close, this, _1));
    ws_server.set_open_handler(std::bind(&WebsocketBroadcaster::on_open, this, _1));
    ws_server.set_message_handler(std::bind(&WebsocketBroadcaster::on_message, this, _1, _2));
    ws_server.set_socket_init_handler(
            std::bind(&WebsocketBroadcaster::on_socket_init, this, _1, _2));
    ws_server.listen(0);
}

void WebsocketBroadcaster::on_message(websocketpp::connection_hdl hdl,
                                      WebsocketServer::message_ptr message) try {
    if (message->get_opcode() != websocketpp::frame::opcode::text) {
        logger->warn("(WebsocketBroadcaster) Got non text message, ignoring");
        return;
    }
    std::string payload = message->get_payload();
    logger->trace("(WebsocketBroadcaster) Got message: {}", payload);
    auto json_msg = json::parse(payload);
    std::string type = json_msg["type"];
    if (type == "SUBSCRIBE") {
        std::string chromecast_name = json_msg["name"];
        logger->debug("(WebsocketBroadcaster) Chromecast {} subscribed", chromecast_name);
        MessageHandler message_handler;
        message_handler.hdl = hdl;
        message_handler.this_ptr = this;
        subscribe_handler(message_handler, chromecast_name);
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
                logger->error("(WebsocketBroadcaster) closing connection failed: {}", ec.message());
            }
        }
    });
}

void WebsocketBroadcaster::start() {
    assert(subscribe_handler != nullptr);

    ws_server.start_accept();

    asio::error_code error;
    auto local_endpoint = ws_server.get_local_endpoint(error);
    if (error) {
        logger->error("(WebsocketBroadcaster) Couldn't get listening socket: {}", error.message());
    } else {
        port = local_endpoint.port();
        logger->info("(WebsocketBroadcaster) Connecting on port {}", port);
    }
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
                                          asio::ip::tcp::socket& s) {
    logger->trace("(WebsocketBroadcaster) Setting new socket tcp::no_delay option");
    asio::ip::tcp::no_delay option(true);
    s.set_option(option);
}

void WebsocketBroadcaster::send_samples(MessageHandler hdl, const AudioSample* samples,
                                        size_t num) {
    if (hdl.this_ptr == nullptr) return;
    std::error_code error;
    // TODO: add checking of buffered amount and ignore send if there is too much
    hdl.this_ptr->ws_server.send(hdl.hdl, samples, num * sizeof(AudioSample),
                                 websocketpp::frame::opcode::binary, error);
    if (error && error != websocketpp::error::value::bad_connection) {
        hdl.this_ptr->logger->error("(WebsocketBroadcaster) Couldn't send data: {}",
                                    error.message());
    }
}
