/* main.cpp -- This file is part of pulseaudio-chromecast-sink
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

#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include <spdlog/spdlog.h>

#include "audio_sinks_manager.h"
#include "chromecast_finder.h"
#include "defer.h"

int main() {
    auto default_logger = spdlog::stdout_logger_mt("default", true /*use color*/);

    default_logger->set_level(spdlog::level::debug);

    boost::asio::io_service io_service;

    AudioSinksManager sinks_manager(io_service);
    std::unordered_map<std::string, std::shared_ptr<AudioSink>> sinks;

    ChromecastFinder finder(io_service, [&](ChromecastFinder::UpdateType type,
                                            ChromecastFinder::ChromecastInfo info) {
        auto print_stuff = [&] {
            std::cout << "\t";
            for (auto& entry : info.dns) {
                std::cout << entry.first << "=" << entry.second << " ";
            }
            std::cout << "\n\t";
            for (auto& entry : info.endpoints) {
                std::cout << entry << " ";
            }
            std::cout << std::endl;
        };

        switch (type) {
            case ChromecastFinder::UpdateType::NEW:
                std::cout << "NEW Chromecast: " << info.name << std::endl;
                print_stuff();
                break;
            case ChromecastFinder::UpdateType::UPDATE:
                std::cout << "UPDATE Chromecast: " << info.name << std::endl;
                print_stuff();
                break;
            case ChromecastFinder::UpdateType::REMOVE:
                std::cout << "REMOVE Chromecast: " << info.name << std::endl;
                break;
        }

        switch (type) {
            case ChromecastFinder::UpdateType::NEW: {
                auto sink = sinks_manager.create_new_sink(info.name);
                sink->set_activation_callback([name = info.name](bool activate) {
                    std::cout << "Chromecast: " << name << " "
                              << (activate ? "Activated!" : "Deactivated!") << std::endl;
                });
                sink->set_volume_callback([name = info.name](double left, double right,
                                                             bool muted) {
                    std::cout << "Chromecast: " << name << " [" << (muted ? "M" : " ")
                              << "] volume: " << left;
                    if (left != right) {
                        std::cout << " " << right;
                    }
                    std::cout << std::endl;
                });
                sinks[info.name] = sink;
                break;
            }
            case ChromecastFinder::UpdateType::UPDATE: break;
            case ChromecastFinder::UpdateType::REMOVE: sinks.erase(info.name); break;
        }
    });

    boost::asio::signal_set signals(io_service, SIGINT, SIGTERM);

    auto stop_everything = [&] {
        finder.stop();
        sinks_manager.stop();
        signals.cancel();
    };

    finder.set_error_handler([&](const std::string& message) {
        default_logger->critical("ChromecastFinder: {}", message);
        stop_everything();
    });

    sinks_manager.set_error_handler([&](const std::string& message) {
        default_logger->critical("AudioSinksManager: {}", message);
        stop_everything();
    });

    signals.async_wait([&](const boost::system::error_code& error, int signal_number) {
        if (error) return;
        default_logger->info("Got signal {}: {}. Exiting...", signal_number,
                             strsignal(signal_number));
        stop_everything();
    });

    std::vector<std::thread> threads;
    for (unsigned i = 0; i < std::thread::hardware_concurrency(); ++i) {
        threads.emplace_back([&io_service]() { io_service.run(); });
    }
    for (auto& t : threads) {
        t.join();
    }

    return 0;
}
