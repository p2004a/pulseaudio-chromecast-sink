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
#include "network_address.h"

ChromecastsManager::ChromecastsManager(boost::asio::io_service& io_service_,
                                       const char* logger_name)
        : io_service(io_service_), chromecasts_strand(io_service),
          sinks_manager(io_service, logger_name),
          finder(io_service,
                 chromecasts_strand.wrap(std::bind(&ChromecastsManager::finder_callback, this,
                                                   std::placeholders::_1, std::placeholders::_2)),
                 logger_name),
          broadcaster(io_service, chromecasts_strand.wrap(std::bind(
                                          &ChromecastsManager::websocket_subscribe_callback, this,
                                          std::placeholders::_1, std::placeholders::_2))),
          error_handler(nullptr) {
    logger = spdlog::get(logger_name);

    finder.set_error_handler([this](const std::string& message) {
        propagate_error("ChromecastFinder: " + message);
    });

    sinks_manager.set_error_handler(
            [&](const std::string& message) { propagate_error("AudioSinksManager: " + message); });
}

void ChromecastsManager::finder_callback(ChromecastFinder::UpdateType type,
                                         ChromecastFinder::ChromecastInfo info) {
    assert(chromecasts_strand.running_in_this_thread());

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

void ChromecastsManager::websocket_subscribe_callback(WebsocketBroadcaster::MessageHandler handler,
                                                      std::string name) {
    assert(chromecasts_strand.running_in_this_thread());

    auto it = chromecasts.find(name);
    if (it != chromecasts.end()) {
        it->second->set_message_handler(handler);
    } else {
        logger->warn("(ChromecastsManager) Chromecast {} subscribed but is not known in manager",
                     name);
    }
}

void ChromecastsManager::stop() {
    finder.stop();
    sinks_manager.stop();
    broadcaster.stop();
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

    sink->set_activation_callback(weak_wrap([this](bool a) { activation_callback(a); }));

    sink->set_volume_callback(
            weak_wrap([this](double l, double r, bool m) { volume_callback(l, r, m); }));

    sink->set_samples_callback([weak_ptr = std::weak_ptr<Chromecast>{shared_from_this()}](
            const AudioSample* s, size_t n) {
        if (auto ptr = weak_ptr.lock()) {
            std::lock_guard<std::mutex> guard(ptr->message_handler_mu);
            WebsocketBroadcaster::send_samples(ptr->message_handler, s, n);
        }
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

void Chromecast::set_message_handler(WebsocketBroadcaster::MessageHandler handler) {
    std::lock_guard<std::mutex> guard(message_handler_mu);
    message_handler = handler;
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
                manager.io_service, *info.endpoints.begin(),
                weak_wrap([this](std::string message) { connection_error_handler(message); }),
                weak_wrap([this](cast_channel::CastMessage message) {
                    connection_message_handler(message);
                }),
                weak_wrap([this](bool connected) { connection_connected_handler(connected); }));
    } else {
        manager.logger->info("(Chromecast '{}') Deactivated!", info.name);
        connection.reset();
        main_channel.reset();
        app_channel.reset();
    }
}

void Chromecast::connection_error_handler(std::string message) {
    manager.logger->error("(Chromecast '{}') connection error: {}", info.name, message);
    connection.reset();
    main_channel.reset();
    app_channel.reset();
    // TODO: try to reconnect after some time
}

void Chromecast::connection_connected_handler(bool connected) {
    if (connected) {
        manager.logger->info("(Chromecast '{}') I'm connected!", info.name);
        main_channel =
                MainChromecastChannel::create(manager.io_service, "sender-0", "receiver-0",
                                              weak_wrap([this](cast_channel::CastMessage msg) {
                                                  if (connection) {
                                                      connection->send_message(msg);
                                                  }
                                              }),
                                              manager.logger->name().c_str());

        main_channel->start();

        // TODO: move appId to config
        main_channel->load_app("10600AB8",
                               weak_wrap([this](nlohmann::json msg) { handle_app_load(msg); }));
    } else {
        manager.logger->info("(Chromecast '{}') I'm not connected!", info.name);
        // TODO: add support for graceful app unloading
        connection.reset();
        main_channel.reset();
        app_channel.reset();
        // TODO: try to reconnect
    }
}

void Chromecast::handle_app_load(nlohmann::json msg) try {
    if (msg["type"] == "LAUNCH_ERROR") {
        manager.logger->error("Failed to launch app");
    } else if (msg["type"] == "RECEIVER_STATUS") {
        transport_id = msg["status"]["applications"][0]["transportId"];
        session_id = msg["status"]["applications"][0]["sessionId"];

        app_channel =
                AppChromecastChannel::create(manager.io_service, "app-controller-0", transport_id,
                                             weak_wrap([this](cast_channel::CastMessage msg) {
                                                 if (connection) {
                                                     connection->send_message(msg);
                                                 }
                                             }),
                                             manager.logger->name().c_str());

        app_channel->start();

        auto addresses = get_local_addresses();
        std::vector<boost::asio::ip::tcp::endpoint> endpoints;
        for (auto addr : addresses) {
            endpoints.emplace_back(addr, manager.broadcaster.get_port());
        }

        app_channel->start_stream(
                endpoints.begin(), endpoints.end(), info.name,
                weak_wrap([this](AppChromecastChannel::Result result) {
                    if (result.ok) {
                        manager.logger->info("(Chromecast) Receiver started streaming!");
                    } else {
                        manager.logger->error("Chromecast Receiver failed to start streaming: {}",
                                              result.message);
                    }
                }));
    }
} catch (std::domain_error) {
    manager.logger->error("(Chromecast) JSON load app didn't have expected fields");
}

void Chromecast::connection_message_handler(const cast_channel::CastMessage& message) {
    if (main_channel &&
        (message.destination_id() == "sender-0" || message.destination_id() == "*")) {
        main_channel->dispatch_message(message);
    }
    if (app_channel &&
        (message.destination_id() == "app-controller-0" || message.destination_id() == "*")) {
        app_channel->dispatch_message(message);
    }
}
