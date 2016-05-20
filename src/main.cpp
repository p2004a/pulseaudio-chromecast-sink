#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include <spdlog/spdlog.h>

#include "chromecast_finder.h"
#include "defer.h"

int main() {
    auto default_logger = spdlog::stdout_logger_mt("default", true /*use color*/);

    default_logger->set_level(spdlog::level::debug);

    boost::asio::io_service io_service;

    ChromecastFinder finder(io_service, [](ChromecastFinder::UpdateType type,
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
    });

    std::vector<std::thread> threads;
    for (unsigned i = 0; i < std::thread::hardware_concurrency(); ++i) {
        threads.emplace_back(std::bind(static_cast<std::size_t (boost::asio::io_service::*)()>(
                                               &boost::asio::io_service::run),
                                       &io_service));
    }
    for (auto& t : threads) {
        t.join();
    }

    return 0;
}
