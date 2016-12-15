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

#include <asio.hpp>

#include <spdlog/spdlog.h>

#include "chromecasts_manager.h"

int main() {
    auto default_logger = spdlog::stdout_logger_mt("default", true /*use color*/);

    default_logger->set_level(spdlog::level::debug);

    asio::io_service io_service;

    ChromecastsManager manager(io_service, "default");

    asio::signal_set signals(io_service, SIGINT, SIGTERM);

    auto stop_everything = [&] {
        manager.stop();
        signals.cancel();
    };

    manager.set_error_handler([&](const std::string& message) {
        default_logger->critical("ChromecastsManager: {}", message);
        stop_everything();
    });

    signals.async_wait([&](const asio::error_code& error, int signal_number) {
        if (error) return;
        default_logger->info("Got signal {}: {}. Exiting...", signal_number,
                             strsignal(signal_number));
        stop_everything();
    });

    manager.start();

    std::vector<std::thread> threads;
    for (unsigned i = 0; i < std::thread::hardware_concurrency(); ++i) {
        threads.emplace_back([&io_service]() { io_service.run(); });
    }
    for (auto& t : threads) {
        t.join();
    }

    return 0;
}
