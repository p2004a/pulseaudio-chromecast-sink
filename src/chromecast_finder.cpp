/* chromecast_finder.cpp -- This file is part of pulseaudio-chromecast-sink
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
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <spdlog/spdlog.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>

#include "chromecast_finder.h"
#include "defer.h"

ChromecastFinder::ChromecastFinder(boost::asio::io_service& io_service_,
                                   std::function<void(UpdateType, ChromecastInfo)> update_callback_,
                                   const char* logger_name)
        : io_service(io_service_), poll(io_service), update_callback(update_callback_),
          error_handler(nullptr) {
    logger = spdlog::get(logger_name);
    poll.get_strand().post([this] { start_discovery(); });
}

ChromecastFinder::~ChromecastFinder() {
    assert(avahi_client == nullptr && "Tried to destruct running instance of ChromecastFinder");
}

void ChromecastFinder::stop() {
    poll.get_strand().dispatch([this]() {
        logger->trace("(ChromecastFinder) Stopping");
        if (stopped) {
            logger->trace("(ChromecastFinder) Already stopped!");
            return;
        }
        stopped = true;
        while (!resolvers.empty()) {
            auto resolverId = resolvers.begin()->first;
            remove_resolver(resolverId);
        }
        if (avahi_browser != nullptr) {
            logger->trace("(ChromecastFinder) Freeing avahi_browser");
            avahi_service_browser_free(avahi_browser);
            avahi_browser = nullptr;
        }
        if (avahi_client != nullptr) {
            logger->trace("(ChromecastFinder) Freeing avahi_client");
            avahi_client_free(avahi_client);
            avahi_client = nullptr;
        }
        logger->debug("(ChromecastFinder) Stopped running");
    });
}

void ChromecastFinder::set_error_handler(std::function<void(const std::string&)> error_handler_) {
    error_handler = error_handler_;
}

void ChromecastFinder::start_discovery() {
    stopped = false;
    int error;
    avahi_client = avahi_client_new(poll.get_pool(), AVAHI_CLIENT_NO_FAIL,
                                    ChromecastFinder::client_callback, this, &error);
    if (stopped) {
        avahi_client = nullptr;
        return;
    }
    if (!avahi_client) {
        report_error("Couldn't create avahi client: " + std::string(avahi_strerror(error)));
    }
}

void ChromecastFinder::client_callback(AvahiClient* c, AvahiClientState state, void* data) {
    ChromecastFinder* cf = static_cast<ChromecastFinder*>(data);
    if (cf->avahi_client != c) {
        cf->avahi_client = c;
    }

    assert(c);

    switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            cf->logger->info("(ChromecastFinder) Connected to Avahi server");
            cf->avahi_browser = avahi_service_browser_new(cf->avahi_client, AVAHI_IF_UNSPEC,
                                                          AVAHI_PROTO_UNSPEC, "_googlecast._tcp",
                                                          "local", static_cast<AvahiLookupFlags>(0),
                                                          ChromecastFinder::browse_callback, cf);
            if (!cf->avahi_browser) {
                cf->report_error("Failed to create service browser: " + cf->get_avahi_error());
            }
            break;

        case AVAHI_CLIENT_S_REGISTERING:
        case AVAHI_CLIENT_S_COLLISION: break;

        case AVAHI_CLIENT_CONNECTING:
            cf->logger->info("(ChromecastFinder) Connecting to Avahi server...");
            break;

        case AVAHI_CLIENT_FAILURE:
            if (avahi_client_errno(cf->avahi_client) == AVAHI_ERR_DISCONNECTED) {
                cf->logger->info("(ChromecastFinder) Avahi server disconnected");
                cf->stop();
                cf->start_discovery();
            } else {
                cf->report_error("Server connection failure: " + cf->get_avahi_error());
            }
            break;
    }
}

void ChromecastFinder::browse_callback(AvahiServiceBrowser* b, AvahiIfIndex interface,
                                       AvahiProtocol protocol, AvahiBrowserEvent event,
                                       const char* name, const char* type, const char* domain,
                                       AvahiLookupResultFlags, void* data) {
    ChromecastFinder* cf = static_cast<ChromecastFinder*>(data);
    assert(b);

    /* Called whenever a new services becomes available on the LAN or is removed from the LAN */

    switch (event) {
        case AVAHI_BROWSER_FAILURE:
            cf->report_error("Browser failure: " + cf->get_avahi_error());
            break;

        case AVAHI_BROWSER_NEW: {
            cf->logger->debug(
                    "(ChromecastFinder) (Browser) New service discovered name: {} interface: {}, "
                    "protocol: {}",
                    name, interface, protocol);

            AvahiServiceResolver* resolver = avahi_service_resolver_new(
                    cf->avahi_client, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC,
                    static_cast<AvahiLookupFlags>(0), ChromecastFinder::resolve_callback, cf);
            if (!resolver) {
                cf->report_error("Failed to create service resolver: " + cf->get_avahi_error());
            } else {
                cf->add_resolver(ResolverId(interface, protocol, name), resolver);
            }
            break;
        }

        case AVAHI_BROWSER_REMOVE: {
            cf->logger->debug(
                    "(ChromecastFinder) (Browser) Service dissapeared name: '{}' interface: {}, "
                    "protocol: {}",
                    name, interface, protocol);
            cf->remove_resolver(ResolverId(interface, protocol, name));
            break;
        }

        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED: break;
    }
}

boost::asio::ip::tcp::endpoint ChromecastFinder::avahiAddresToAsioEndpoint(
        const AvahiAddress* address, uint16_t port) {
    char addr_str[AVAHI_ADDRESS_STR_MAX];
    avahi_address_snprint(addr_str, sizeof(addr_str), address);
    return boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(addr_str), port);
}

std::map<std::string, std::string> ChromecastFinder::avahiDNSStringListToMap(
        AvahiStringList* node) {
    std::map<std::string, std::string> result;
    for (; node; node = node->next) {
        size_t eq_pos;
        for (eq_pos = 0; eq_pos < node->size; ++eq_pos) {
            if (node->text[eq_pos] == '=') {
                break;
            }
        }
        if (eq_pos == node->size) {
            logger->warn(
                    "(ChromecastFinder) Avahi DNS string element didn't contain equal sign, "
                    "ignoring");
            continue;
        }
        result.insert({std::string((char*)node->text, eq_pos),
                       std::string((char*)node->text + eq_pos + 1, node->size - eq_pos - 1)});
    }
    return result;
};

void ChromecastFinder::resolve_callback(AvahiServiceResolver* r, AvahiIfIndex interface,
                                        AvahiProtocol protocol, AvahiResolverEvent event,
                                        const char* name, const char* /*type*/,
                                        const char* /*domain*/, const char* /*host_name*/,
                                        const AvahiAddress* address, uint16_t port,
                                        AvahiStringList* txt, AvahiLookupResultFlags, void* data) {
    assert(r);
    ChromecastFinder* cf = static_cast<ChromecastFinder*>(data);

    switch (event) {
        case AVAHI_RESOLVER_FAILURE:
            cf->logger->error(
                    "(ChromecastFinder) (Resolver) Failed to resolve service name: '{}' interface: "
                    "{} protocol: "
                    "{}: {}",
                    name, interface, protocol, cf->get_avahi_error());
            cf->remove_resolver(ResolverId(interface, protocol, name));
            break;

        case AVAHI_RESOLVER_FOUND: {
            cf->logger->debug(
                    "(ChromecastFinder) (Resolver) Resolved service name: '{}' interface: {} "
                    "protocol: {}",
                    name, interface, protocol);
            auto endpoint = cf->avahiAddresToAsioEndpoint(address, port);
            auto dns = cf->avahiDNSStringListToMap(txt);
            std::string chromecast_name(name);
            cf->chromecasts_update(r, name, endpoint, dns);
            break;
        }
    }
}

void ChromecastFinder::report_error(const std::string& message) {
    if (error_handler) {
        error_handler(message);
    } else {
        throw ChromecastFinderException(message);
    }
}

std::string ChromecastFinder::get_avahi_error() const {
    return avahi_strerror(avahi_client_errno(avahi_client));
}

void ChromecastFinder::remove_resolver(ResolverId id) {
    logger->trace("(ChromecastFinder) Remove resolver: {} {} {}", id.name, id.interface,
                  id.protocol);
    auto resolver_it = resolvers.find(id);
    assert(resolver_it != resolvers.end());
    AvahiServiceResolver* resolver = resolver_it->second;
    avahi_service_resolver_free(resolver);
    resolvers.erase(resolver_it);
    chromecasts_remove(resolver);
}

void ChromecastFinder::add_resolver(ResolverId id, AvahiServiceResolver* resolver) {
    logger->trace("(ChromecastFinder) Add resolver: {} {} {}", id.name, id.interface, id.protocol);
    bool inserted = resolvers.insert({id, resolver}).second;
    assert(inserted);
}

void ChromecastFinder::chromecasts_update(AvahiServiceResolver* resolver, const std::string& name,
                                          const boost::asio::ip::tcp::endpoint& endpoint,
                                          const std::map<std::string, std::string>& dns) {
    bool added = false, updated = false;

    InternalChromecastInfo* chromecast;
    auto chromecast_it = chromecasts.find(name);
    if (chromecast_it == chromecasts.end()) {
        chromecast = new InternalChromecastInfo();
        chromecast->name = name;
        chromecasts.emplace(name, std::unique_ptr<InternalChromecastInfo>(chromecast));
        added = true;
    } else {
        chromecast = chromecast_it->second.get();
    }

    if (dns != chromecast->dns) {
        chromecast->dns = dns;
        updated = true;
    }

    bool set_endpoint = false;
    if (resolver_to_chromecast.find(resolver) == resolver_to_chromecast.end()) {
        resolver_to_chromecast[resolver] = chromecast;
        set_endpoint = true;
    } else {
        auto curr_endpoint = chromecast->endpoints[resolver];
        if (curr_endpoint != endpoint) {
            auto endpoint_count_it = chromecast->endpoint_count.find(curr_endpoint);
            if (--endpoint_count_it->second == 0) {
                chromecast->endpoint_count.erase(endpoint_count_it);
                updated = true;
            }
            set_endpoint = true;
        }
    }

    if (set_endpoint) {
        chromecast->endpoints[resolver] = endpoint;
        auto endpoint_count_it = chromecast->endpoint_count.find(endpoint);
        if (endpoint_count_it == chromecast->endpoint_count.end()) {
            chromecast->endpoint_count[endpoint] = 1;
            updated = true;
        } else {
            endpoint_count_it->second += 1;
        }
    }

    if (added || updated) {
        send_update(added ? UpdateType::NEW : UpdateType::UPDATE, chromecast);
    }
}

void ChromecastFinder::chromecasts_remove(AvahiServiceResolver* resolver) {
    auto resolver_to_chromecast_it = resolver_to_chromecast.find(resolver);
    if (resolver_to_chromecast_it == resolver_to_chromecast.end()) {
        // This is possible when someone calls ChromecastFinder::stop after resolver was registered
        // but before it resolved service.
        return;
    }

    bool updated = false;
    auto chromecast = resolver_to_chromecast_it->second;
    resolver_to_chromecast.erase(resolver_to_chromecast_it);

    auto endpoints_it = chromecast->endpoints.find(resolver);
    auto endpoint_count_it = chromecast->endpoint_count.find(endpoints_it->second);
    if (--endpoint_count_it->second == 0) {
        chromecast->endpoint_count.erase(endpoint_count_it);
        updated = true;
    }
    chromecast->endpoints.erase(endpoints_it);

    if (chromecast->endpoints.empty()) {
        send_update(UpdateType::REMOVE, chromecast);
        chromecasts.erase(chromecast->name);
    } else if (updated) {
        send_update(UpdateType::UPDATE, chromecast);
    }
}

void ChromecastFinder::send_update(UpdateType type, InternalChromecastInfo* chromecast) const {
    static const char* update_name[] = {"NEW", "UPDATE", "REMOVE"};
    logger->trace("(ChromecastFinder) Sending update {} {}", chromecast->name,
                  update_name[static_cast<int>(type)]);
    ChromecastInfo info;
    info.name = chromecast->name;
    info.dns = chromecast->dns;
    for (const auto& elem : chromecast->endpoint_count) {
        info.endpoints.insert(elem.first);
    }
    update_callback(type, info);
}
