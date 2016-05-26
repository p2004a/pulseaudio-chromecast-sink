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

    default_logger->set_level(spdlog::level::trace);

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
            case ChromecastFinder::UpdateType::NEW:
                sinks[info.name] = sinks_manager.create_new_sink(info.name);
                break;
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
