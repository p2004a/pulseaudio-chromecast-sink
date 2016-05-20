#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>

#include "chromecast_finder.h"
#include "defer.h"

ChromecastFinder::ChromecastFinder(boost::asio::io_service& io_service_,
                                   std::function<void(UpdateType, ChromecastInfo)> update_callback_)
        : io_service(io_service_), poll(io_service), update_callback(update_callback_) {
    poll.get_strand().post(std::bind(&ChromecastFinder::start_discovery, this));
}

ChromecastFinder::~ChromecastFinder() {
    if (avahi_client != nullptr) {
        throw std::runtime_error("Tried to destruct running instance of ChromecastFinder");
    }
}

void ChromecastFinder::stop() {
    poll.get_strand().dispatch([this]() {
        if (stopped) return;
        stopped = true;
        while (!resolvers.empty()) {
            auto resolverId = resolvers.begin()->first;
            remove_resolver(resolverId);
        }
        if (avahi_browser != nullptr) {
            avahi_service_browser_free(avahi_browser);
            avahi_browser = nullptr;
        }
        if (avahi_client != nullptr) {
            avahi_client_free(avahi_client);
            avahi_client = nullptr;
        }
    });
}

void ChromecastFinder::start_discovery() {
    stopped = false;
    int error;
    avahi_client = avahi_client_new(poll.get_pool(), AVAHI_CLIENT_NO_FAIL,
                                    ChromecastFinder::client_callback, this, &error);
    if (!avahi_client) {
        throw std::runtime_error("Couldn't create avahi client: " +
                                 std::string(avahi_strerror(error)));
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
            cf->avahi_browser = avahi_service_browser_new(cf->avahi_client, AVAHI_IF_UNSPEC,
                                                          AVAHI_PROTO_UNSPEC, "_googlecast._tcp",
                                                          "local", static_cast<AvahiLookupFlags>(0),
                                                          ChromecastFinder::browse_callback, cf);
            if (!cf->avahi_browser) {
                throw std::runtime_error("Failed to create service browser: " +
                                         cf->get_avahi_error());
            }
            break;

        case AVAHI_CLIENT_S_REGISTERING:
        case AVAHI_CLIENT_S_COLLISION: break;

        case AVAHI_CLIENT_CONNECTING:
            std::cerr << "Connecting to Avahi server..." << std::endl;
            break;

        case AVAHI_CLIENT_FAILURE:
            if (avahi_client_errno(cf->avahi_client) == AVAHI_ERR_DISCONNECTED) {
                cf->stop();
                cf->start_discovery();
            } else {
                throw std::runtime_error("Server connection failure: " + cf->get_avahi_error());
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
            throw std::runtime_error("Browser failure: " + cf->get_avahi_error());

        case AVAHI_BROWSER_NEW: {
            AvahiServiceResolver* resolver = avahi_service_resolver_new(
                    cf->avahi_client, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC,
                    static_cast<AvahiLookupFlags>(0), ChromecastFinder::resolve_callback, cf);
            if (!resolver) {
                throw std::runtime_error("Failed to create service resolver: " +
                                         cf->get_avahi_error());
            }

            cf->add_resolver(ResolverId(interface, protocol, name), resolver);
            break;
        }

        case AVAHI_BROWSER_REMOVE: {
            cf->remove_resolver(ResolverId(interface, protocol, name));
            break;
        }

        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED: break;
    }
}

boost::asio::ip::tcp::endpoint avahiAddresToAsioEndpoint(const AvahiAddress* address,
                                                         uint16_t port) {
    char addr_str[AVAHI_ADDRESS_STR_MAX];
    avahi_address_snprint(addr_str, sizeof(addr_str), address);
    return boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(addr_str), port);
}

std::map<std::string, std::string> avahiDNSStringListToMap(AvahiStringList* node) {
    std::map<std::string, std::string> result;
    for (; node; node = node->next) {
        size_t eq_pos;
        for (eq_pos = 0; eq_pos < node->size; ++eq_pos) {
            if (node->text[eq_pos] == '=') {
                break;
            }
        }
        if (eq_pos == node->size) {
            throw std::logic_error("Avahi DNS string element didn't contain equal sign");
        }
        result.insert({std::string((char*)node->text, eq_pos),
                       std::string((char*)node->text + eq_pos + 1, node->size - eq_pos - 1)});
    }
    return result;
};

void ChromecastFinder::resolve_callback(AvahiServiceResolver* r, AvahiIfIndex interface,
                                        AvahiProtocol protocol, AvahiResolverEvent event,
                                        const char* name, const char* type, const char* domain,
                                        const char* /*host_name*/, const AvahiAddress* address,
                                        uint16_t port, AvahiStringList* txt, AvahiLookupResultFlags,
                                        void* data) {
    assert(r);
    ChromecastFinder* cf = static_cast<ChromecastFinder*>(data);

    switch (event) {
        case AVAHI_RESOLVER_FAILURE:
            std::cerr << "(Resolver) Failed to resolve service '" << name << "' of type '" << type
                      << "' in domain '" << domain << "': " << cf->get_avahi_error() << std::endl;

            cf->remove_resolver(ResolverId(interface, protocol, name));
            break;

        case AVAHI_RESOLVER_FOUND: {
            auto endpoint = avahiAddresToAsioEndpoint(address, port);
            auto dns = avahiDNSStringListToMap(txt);
            std::string chromecast_name(name);
            cf->chromecasts_update(r, name, endpoint, dns);
            break;
        }
    }
}

std::string ChromecastFinder::get_avahi_error() const {
    return avahi_strerror(avahi_client_errno(avahi_client));
}

void ChromecastFinder::remove_resolver(ResolverId id) {
    auto resolver_it = resolvers.find(id);
    if (resolver_it == resolvers.end()) {
        throw std::logic_error("Couldn't remove resolver, it wasn't there");
    } else {
        AvahiServiceResolver* resolver = resolver_it->second;
        avahi_service_resolver_free(resolver);
        resolvers.erase(resolver_it);
        chromecasts_remove(resolver);
    }
}

void ChromecastFinder::add_resolver(ResolverId id, AvahiServiceResolver* resolver) {
    bool inserted = resolvers.insert({id, resolver}).second;
    if (!inserted) {
        throw std::logic_error("Couldn't insert resolver because it's already there");
    }
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
    ChromecastInfo info;
    info.name = chromecast->name;
    info.dns = chromecast->dns;
    for (const auto& elem : chromecast->endpoint_count) {
        info.endpoints.insert(elem.first);
    }
    update_callback(type, info);
}
