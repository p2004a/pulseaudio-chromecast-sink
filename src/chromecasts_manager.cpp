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

#include <gflags/gflags.h>

#include "chromecasts_manager.h"
#include "network_address.h"

DEFINE_string(chromecast_app_id, "10600AB8", "id of the app to load to Chromecast");

ChromecastsManager::ChromecastsManager(asio::io_service& io_service_, const char* logger_name)
        : io_service(io_service_), chromecasts_strand(io_service),
          sinks_manager(io_service, logger_name), finder(io_service, logger_name),
          broadcaster(io_service, logger_name), error_handler(nullptr) {
    logger = spdlog::get(logger_name);

    finder.set_update_handler(chromecasts_strand.wrap(
            [this](ChromecastFinder::UpdateType type, ChromecastFinder::ChromecastInfo info) {
                finder_callback(type, info);
            }));

    finder.set_error_handler([this](const std::string& message) {
        propagate_error("ChromecastFinder: " + message);
    });

    sinks_manager.set_error_handler(
            [&](const std::string& message) { propagate_error("AudioSinksManager: " + message); });

    broadcaster.set_subscribe_handler(chromecasts_strand.wrap(
            [this](WebsocketBroadcaster::MessageHandler handler, std::string name) {
                websocket_subscribe_callback(handler, name);
            }));
}

void ChromecastsManager::finder_callback(ChromecastFinder::UpdateType type,
                                         ChromecastFinder::ChromecastInfo info) {
    assert(chromecasts_strand.running_in_this_thread());

    switch (type) {
        case ChromecastFinder::UpdateType::NEW: {
            logger->info("(ChromecastsManager) New Chromecast '{}'", info.name);
            auto chromecast = Chromecast::create(*this, info);
            chromecast->start();
            chromecasts[info.name] = chromecast;
            break;
        }
        case ChromecastFinder::UpdateType::UPDATE: {
            chromecasts[info.name]->update_info(info);
            break;
        }
        case ChromecastFinder::UpdateType::REMOVE: {
            auto it = chromecasts.find(info.name);
            if (it != chromecasts.end()) {
                it->second->stop();
                chromecasts.erase(it);
                logger->info("(ChromecastsManager) Chromecast '{}' removed", info.name);
            } else {
                logger->info(
                        "(ChromecastsManager) Chromecast '{}' requested to remove, but not "
                        "existing",
                        info.name);
            }

            break;
        }
    }
}

void ChromecastsManager::websocket_subscribe_callback(WebsocketBroadcaster::MessageHandler handler,
                                                      std::string name) {
    assert(chromecasts_strand.running_in_this_thread());

    auto it = chromecasts.find(name);
    if (it != chromecasts.end()) {
        it->second->set_message_handler(handler);
    } else {
        logger->warn("(ChromecastsManager) Chromecast '{}' subscribed but is not known in manager",
                     name);
    }
}

void ChromecastsManager::start() {
    broadcaster.start();
    sinks_manager.start();
    finder.start();
}

void ChromecastsManager::stop() {
    finder.stop();
    sinks_manager.stop();
    broadcaster.stop();
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

void Chromecast::start() {
    std::string* pretty_name = &info.name;
    auto it = info.dns.find("fn");
    if (it != info.dns.end()) {
        pretty_name = &it->second;
    }
    sink = manager.sinks_manager.create_new_sink(info.name, *pretty_name);

    sink->set_activation_callback(mem_weak_wrap(&Chromecast::activation_callback));
    sink->set_volume_callback(mem_weak_wrap(&Chromecast::volume_callback));

    sink->set_samples_callback(wrap_weak_ptr(
            [this](const AudioSample* s, size_t n) {
                std::lock_guard<std::mutex> guard(message_handler_mu);
                WebsocketBroadcaster::send_samples(message_handler, s, n);
            },
            this));
}

void Chromecast::stop() {
    if (connection) {
        connection->stop();
        connection.reset();
    }
    if (main_channel) {
        main_channel.reset();
    }
    if (app_channel) {
        app_channel.reset();
    }
}

std::shared_ptr<Chromecast> Chromecast::create(ChromecastsManager& manager_,
                                               ChromecastFinder::ChromecastInfo info_) {
    return std::make_shared<Chromecast>(manager_, info_, private_tag{});
}

void Chromecast::update_info(ChromecastFinder::ChromecastInfo info_) {
    strand.dispatch(weak_wrap([=] { info = info_; }));
}

void Chromecast::set_message_handler(WebsocketBroadcaster::MessageHandler handler) {
    std::lock_guard<std::mutex> guard(message_handler_mu);
    message_handler = handler;
}

void Chromecast::volume_callback(double left, double right, bool muted) {
    if (left != right) {
        manager.logger->info("(Chromecast '{}') left volume {} != right volume {}", info.name, left,
                             right);
    }
    manager.logger->info("(Chromecast '{}') [{}] volume {}", info.name, muted ? "M" : " ", left);
    // TODO: send volume update to chromecast
}

void Chromecast::activation_callback(bool activate) {
    activated = activate;
    if (activated) {
        manager.logger->info("(Chromecast '{}') Activated!", info.name);
        connection = ChromecastConnection::create(manager.io_service, *info.endpoints.begin());
        connection->set_error_handler(mem_weak_wrap(&Chromecast::connection_error_handler));
        connection->set_connected_handler(mem_weak_wrap(&Chromecast::connection_connected_handler));
        connection->set_messages_handler(mem_weak_wrap(&Chromecast::connection_message_handler));
        connection->start();
    } else {
        manager.logger->info("(Chromecast '{}') Deactivated!", info.name);
        connection->stop();
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
                                              mem_weak_wrap(&Chromecast::connection_message_sender),
                                              manager.logger->name().c_str());

        main_channel->start();

        main_channel->load_app(FLAGS_chromecast_app_id,
                               mem_weak_wrap(&Chromecast::handle_app_load));
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
                                             mem_weak_wrap(&Chromecast::connection_message_sender),
                                             manager.logger->name().c_str());

        app_channel->start();

        auto addresses = get_local_addresses();
        std::vector<asio::ip::tcp::endpoint> endpoints;
        for (auto addr : addresses) {
            endpoints.emplace_back(addr, manager.broadcaster.get_port());
        }

        app_channel->start_stream(endpoints.begin(), endpoints.end(), info.name,
                                  mem_weak_wrap(&Chromecast::handle_stream_start));
    }
} catch (std::domain_error) {
    manager.logger->error("(Chromecast '{}') JSON load app didn't have expected fields", info.name);
}

void Chromecast::handle_stream_start(AppChromecastChannel::Result result) {
    if (result.ok) {
        manager.logger->info("(Chromecast '{}') Receiver started streaming!", info.name);
    } else {
        manager.logger->error("(Chromecast '{}') Receiver failed to start streaming: {}", info.name,
                              result.message);
    }
}

void Chromecast::connection_message_sender(cast_channel::CastMessage message) {
    if (connection) {
        connection->send_message(message);
    }
}

void Chromecast::connection_message_handler(cast_channel::CastMessage message) {
    if (main_channel &&
        (message.destination_id() == "sender-0" || message.destination_id() == "*")) {
        main_channel->dispatch_message(message);
    }
    if (app_channel &&
        (message.destination_id() == "app-controller-0" || message.destination_id() == "*")) {
        app_channel->dispatch_message(message);
    }
}
