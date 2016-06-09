/* network_address.cpp -- This file is part of pulseaudio-chromecast-sink
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_link.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <boost/asio/ip/tcp.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "defer.h"
#include "network_address.h"

std::vector<boost::asio::ip::address> get_local_addresses() {
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        throw std::runtime_error("Couldn't get interface addresses with getifaddrs");
    }
    defer {
        freeifaddrs(ifaddr);
    };

    std::vector<boost::asio::ip::address> result;
    for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }

        int family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) {
            continue;
        }

        if (strcmp(ifa->ifa_name, "lo") == 0) {
            continue;
        }

        char host[NI_MAXHOST];
        int status = getnameinfo(ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in)
                                                                    : sizeof(struct sockaddr_in6),
                                 host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
        if (status != 0) {
            throw std::runtime_error("getnameinfo() failed: " + std::string(gai_strerror(status)));
        }

        result.emplace_back(boost::asio::ip::address::from_string(host));
    }

    return result;
}