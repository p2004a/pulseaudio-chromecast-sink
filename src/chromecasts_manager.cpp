/* chromecasts_manager.cpp -- This file is part of pulseaudio-chromecast-sink
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

#include "chromecasts_manager.h"

ChromecastsManager::ChromecastsManager(boost::asio::io_service& io_service_,
                                       const char* logger_name)
        : io_service(io_service_), sinks_manager(io_service, logger_name),
          finder(io_service, std::bind(&ChromecastsManager::finder_callback, this,
                                       std::placeholders::_1, std::placeholders::_2),
                 logger_name) {
    logger = spdlog::get(logger_name);

    finder.set_error_handler([this](const std::string& message) {
        propagate_error("ChromecastFinder: " + message);
    });

    sinks_manager.set_error_handler(
            [&](const std::string& message) { propagate_error("AudioSinksManager: " + message); });
}

void ChromecastsManager::finder_callback(ChromecastFinder::UpdateType type,
                                         ChromecastFinder::ChromecastInfo info) {
    if (type == ChromecastFinder::UpdateType::NEW) {
        logger->info("New Chromecast '{}'", info.name);
    } else if (type == ChromecastFinder::UpdateType::REMOVE) {
        logger->info("Chromecast '{}' removed", info.name);
    }

    switch (type) {
        case ChromecastFinder::UpdateType::NEW:
            chromecasts[info.name] = Chromecast::create(*this, info);
            break;
        case ChromecastFinder::UpdateType::UPDATE: chromecasts[info.name]->update_info(info); break;
        case ChromecastFinder::UpdateType::REMOVE: chromecasts.erase(info.name); break;
    }
}

void ChromecastsManager::stop() {
    finder.stop();
    sinks_manager.stop();
}

void ChromecastsManager::set_error_handler(ErrorHandler error_handler_) {
    error_handler = error_handler_;
}

void ChromecastsManager::propagate_error(const std::string& message) {
    if (error_handler) {
        error_handler(message);
    } else {
        throw ChromecastsManagerException(message);
    }
}

Chromecast::Chromecast(ChromecastsManager& manager_, ChromecastFinder::ChromecastInfo info_,
                       private_tag)
        : manager(manager_), info(info_), strand(manager.io_service), activated(false) {}

void Chromecast::init() {
    sink = manager.sinks_manager.create_new_sink(info.name);
    sink->set_activation_callback(strand.wrap([weak_ptr = my_weak_from_this()](bool a) {
        if (auto ptr = weak_ptr.lock()) {
            ptr->activation_callback(a);
        }
    }));
    sink->set_volume_callback(
            strand.wrap([weak_ptr = my_weak_from_this()](double l, double r, bool m) {
                if (auto ptr = weak_ptr.lock()) {
                    ptr->volume_callback(l, r, m);
                }
            }));
    sink->set_samples_callback([this](const AudioSample* /*s*/, size_t /*n*/) {
        // TODO: Handle samples
    });
}

std::shared_ptr<Chromecast> Chromecast::create(ChromecastsManager& manager_,
                                               ChromecastFinder::ChromecastInfo info_) {
    auto res = std::make_shared<Chromecast>(manager_, info_, private_tag{});
    res->init();
    return res;
}

void Chromecast::update_info(ChromecastFinder::ChromecastInfo info_) {
    strand.dispatch([ =, this_ptr = shared_from_this() ] { info = info_; });
}

void Chromecast::volume_callback(double left, double right, bool muted) {
    if (left != right) {
        manager.logger->notice("(Chromecast '{}') left volume {} != right volume {}", info.name,
                               left, right);
    }
    manager.logger->info("(Chromecast '{}') [{}] volume {}", info.name, muted ? "M" : " ", left);
    // TODO: send volume update to chromecast
}

void Chromecast::activation_callback(bool activate) {
    activated = activate;
    if (activated) {
        manager.logger->info("(Chromecast '{}') Activated!", info.name);
        connection = std::make_shared<ChromecastConnection>(
                manager.io_service, info.endpoints.begin(), info.endpoints.end(),
                strand.wrap([weak_ptr = my_weak_from_this()](std::string message) {
                    if (auto ptr = weak_ptr.lock()) {
                        ptr->connection_error_handler(message);
                    }
                }),
                strand.wrap([weak_ptr = my_weak_from_this()](cast_channel::CastMessage message) {
                    if (auto ptr = weak_ptr.lock()) {
                        ptr->connection_message_handler(message);
                    }
                }),
                strand.wrap([weak_ptr = my_weak_from_this()](bool connected) {
                    if (auto ptr = weak_ptr.lock()) {
                        ptr->connection_connected_handler(connected);
                    }
                }));
    } else {
        manager.logger->info("(Chromecast '{}') Deactivated!", info.name);
        connection.reset();
    }
}

void Chromecast::samples_callback(const AudioSample* /*samples*/, size_t /*num*/) {
    // TODO: handle samples
}

void Chromecast::connection_error_handler(std::string message) {
    manager.logger->error("(Chromecast '{}') connection error: {}", info.name, message);
    connection.reset();
    // TODO: try to reconnect after some time
}

void Chromecast::connection_connected_handler(bool connected) {
    if (connected) {
        manager.logger->info("(Chromecast '{}') I'm connected!", info.name);

        cast_channel::CastMessage message;
        message.set_protocol_version(cast_channel::CastMessage_ProtocolVersion_CASTV2_1_0);
        message.set_source_id("sender-0");
        message.set_destination_id("receiver-0");

        message.set_namespace_("urn:x-cast:com.google.cast.tp.connection");
        message.set_payload_type(cast_channel::CastMessage_PayloadType_STRING);
        message.set_payload_utf8("{\"type\":\"CONNECT\"}");
        connection->send_message(message);

        message.set_namespace_("urn:x-cast:com.google.cast.tp.heartbeat");
        message.set_payload_type(cast_channel::CastMessage_PayloadType_STRING);
        message.set_payload_utf8("{\"type\":\"PING\"}");
        connection->send_message(message);
    } else {
        manager.logger->info("(Chromecast '{}') I'm not connected!", info.name);
    }
    // TODO: start protcol
}

void Chromecast::connection_message_handler(const cast_channel::CastMessage& message) {
    manager.logger->trace("(Chromecast '{}') Got message: {}", info.name, message.DebugString());
    // TODO: dispatch message
}
