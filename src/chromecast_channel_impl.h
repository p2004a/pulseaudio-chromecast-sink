/* chromecast_channel_impl.h -- This file is part of pulseaudio-chromecast-sink
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

#include <asio/ip/tcp.hpp>
#include <sstream>

#include "chromecast_channel.h"

template <class T>
BaseChromecastChannel<T>::BaseChromecastChannel(asio::io_service& io_service, std::string name_,
                                                std::string destination_, MessageFunc send_func_,
                                                const char* logger_name, private_tag)
        : strand(io_service), name(name_), destination(destination_), send_func(send_func_) {
    logger = spdlog::get(logger_name);
}

template <class T>
std::shared_ptr<T> BaseChromecastChannel<T>::create(asio::io_service& io_service, std::string name_,
                                                    std::string destination_,
                                                    MessageFunc send_func_,
                                                    const char* logger_name) {
    return std::make_shared<T>(io_service, name_, destination_, send_func_, logger_name,
                               private_tag{});
}

template <class T>
void BaseChromecastChannel<T>::dispatch_message(cast_channel::CastMessage message) {
    weak_dispatch([message, this] { real_message_dispatch(message); });
}

template <class T>
void BaseChromecastChannel<T>::real_message_dispatch(const cast_channel::CastMessage& message) {
    const char* ns = message.namespace_().c_str();
    if (message.source_id() != destination && message.destination_id() != "*") {
        logger->warn("(BaseChromecastChannel) Got message from unexpected sender '{}'",
                     message.source_id());
    } else if (message.payload_type() != cast_channel::CastMessage::STRING) {
        logger->warn("(BaseChromecastChannel) Got BINARY payload type");
    } else if (!message.has_payload_utf8()) {
        logger->warn("(BaseChromecastChannel) Message didn't have any payload!");
    } else {
        auto it = namespace_handlers.find(ns);
        if (it != namespace_handlers.end()) {
            try {
                auto json_msg = nlohmann::json::parse(message.payload_utf8());
                it->second(json_msg);
            } catch (std::invalid_argument) {
                logger->warn("(BaseChromecastChannel) Couldn't parse message payload as JSON", ns);
            }
        } else if (message.destination_id() != "*") {
            logger->warn("(BaseChromecastChannel) Unexpected namespace in channel '{}'", ns);
        }
    }
}

template <class T>
void BaseChromecastChannel<T>::register_namespace_callback(std::string ns, ParsedMessageFunc func) {
    namespace_handlers[ns] = func;
}

template <class T>
void BaseChromecastChannel<T>::send_message(std::string ns, nlohmann::json msg) {
    cast_channel::CastMessage message;
    message.set_protocol_version(cast_channel::CastMessage_ProtocolVersion_CASTV2_1_0);
    message.set_source_id(name);
    message.set_destination_id(destination);
    message.set_namespace_(ns);
    message.set_payload_type(cast_channel::CastMessage_PayloadType_STRING);
    message.set_payload_utf8(msg.dump());

    send_func(message);
}

template <class T>
BasicChromecastChannel<T>::BasicChromecastChannel(
        asio::io_service& io_service, std::string name_, std::string destination_,
        typename BaseChromecastChannel<T>::MessageFunc send_func_, const char* logger_name,
        typename BaseChromecastChannel<T>::private_tag tag)
        : BaseChromecastChannel<T>(io_service, name_, destination_, send_func_, logger_name, tag),
          timer(io_service) {
    this->register_namespace_callback(CHCHANNS_CONNECTION,
                                      [this](nlohmann::json msg) { handle_connect_channel(msg); });
    this->register_namespace_callback(
            CHCHANNS_HEARTBEAT, [this](nlohmann::json msg) { handle_heartbeat_channel(msg); });
}

template <class T>
void BasicChromecastChannel<T>::start() {
    this->weak_dispatch([this] {
        nlohmann::json connect_msg = {{"type", "CONNECT"}};
        this->send_message(CHCHANNS_CONNECTION, connect_msg);
        asio::error_code error;
        this->timer_expired_callback(error);
    });
}

template <class T>
void BasicChromecastChannel<T>::handle_connect_channel(nlohmann::json msg) try {
    std::string type = msg["type"];
    this->logger->debug("(BasidChromecastChannel) Got quite unexpected {} message", type);
    if (type == "CONNECT") {
        // TODO: handle connect message, which is quite unexpected
    } else if (type == "CLOSE") {
        // TODO: handle cloce message, maybe just ignore it?
    } else {
        this->logger->warn("(BasidChromecastChannel) Unrecognized ns connect type: {}", type);
    }
} catch (std::domain_error) {
    this->logger->warn("(BasicChromecastChannel) JSON connect ns, didn't have expected fields");
}

template <class T>
void BasicChromecastChannel<T>::handle_heartbeat_channel(nlohmann::json msg) try {
    std::string type = msg["type"];
    if (type == "PING") {
        nlohmann::json pong_msg = {{"type", "PONG"}};
        this->send_message(CHCHANNS_HEARTBEAT, pong_msg);
    } else if (type == "PONG") {
        // TODO: handle pong messages
    } else {
        this->logger->warn("(BasidChromecastChannel) Unrecognized ns heartbeat type: {}", type);
    }
} catch (std::domain_error) {
    this->logger->warn("(BasicChromecastChannel) JSON heartbeat ns, didn't have expected fields");
}

template <class T>
void BasicChromecastChannel<T>::timer_expired_callback(const asio::error_code& error) {
    if (error) return;
    nlohmann::json ping_msg = {{"type", "PING"}};
    this->send_message(CHCHANNS_HEARTBEAT, ping_msg);

    // TODO: make this deadline configurable
    timer.expires_from_now(std::chrono::seconds(20));
    timer.async_wait(this->weak_wrap(
            [this](const asio::error_code& error) { timer_expired_callback(error); }));
}

template <class It>
void AppChromecastChannel::start_stream(It begin, It end, std::string device_name,
                                        ResultCb result_callback) {
    int request_id = curr_request_id++;
    nlohmann::json start_stream_msg = {{"type", "START_STREAM"},
                                       {"requestId", request_id},
                                       {"addresses", nlohmann::json::array()},
                                       {"deviceName", device_name}};
    for (It it = begin; it != end; ++it) {
        asio::ip::tcp::endpoint endpoint = *it;
        std::stringstream ss;
        ss << "ws://" << endpoint;

        start_stream_msg["addresses"].push_back(ss.str());
    }
    pending_requests[request_id] = result_callback;
    send_message(CHCHANNS_STREAM_APP, start_stream_msg);
}
