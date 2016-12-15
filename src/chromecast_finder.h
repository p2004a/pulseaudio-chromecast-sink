/* chromecast_finder.h -- This file is part of pulseaudio-chromecast-sink
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

#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include <asio/io_service.hpp>
#include <asio/ip/tcp.hpp>

#include <spdlog/spdlog.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include "asio_avahi_poll.h"
#include "util.h"

class ChromecastFinderException : public std::runtime_error {
  public:
    ChromecastFinderException(std::string message) : std::runtime_error(message) {}
};

class ChromecastFinder {
  public:
    enum class UpdateType { NEW, UPDATE, REMOVE };

    struct ChromecastInfo {
        std::string name;
        std::set<asio::ip::tcp::endpoint> endpoints;
        std::map<std::string, std::string> dns;
    };

    typedef std::function<void(UpdateType, ChromecastInfo)> UpdateHandler;
    typedef std::function<void(const std::string&)> ErrorHandler;

    ChromecastFinder(asio::io_service& io_service_, const char* logger_name = "default");

    ~ChromecastFinder();

    void start();
    void stop();

    // This function have to be called *before* starting event loop!
    void set_error_handler(ErrorHandler error_handler_) {
        error_handler = error_handler_;
    }

    void set_update_handler(UpdateHandler update_handler_) {
        update_handler = update_handler_;
    }

  private:
    /*
     * This class is necessary because we need to be able to free resolver when
     * service disappear. This information is firstly received through ServiceBrowser
     * which needs map from resolver metadata to resolver object pointer: resolvers member.
     */
    struct ResolverId {
        ResolverId(AvahiIfIndex interface_, AvahiProtocol protocol_, std::string name_)
                : interface(interface_), protocol(protocol_), name(name_) {}

        bool operator==(const ResolverId& other) const {
            return interface == other.interface && protocol == other.protocol && name == other.name;
        }

        AvahiIfIndex interface;
        AvahiProtocol protocol;
        std::string name;
        // We don't care about domain and type because they are always the same for every resolver.
    };

    struct ResolverIdHash {
        std::size_t operator()(const ResolverId& id) const {
            return hash_combine(id.protocol, id.interface, id.name);
        }
    };

    struct InternalChromecastInfo {
        std::string name;
        std::map<std::string, std::string> dns;
        std::map<asio::ip::tcp::endpoint, int> endpoint_count;
        std::map<AvahiServiceResolver*, asio::ip::tcp::endpoint> endpoints;
    };

    void start_discovery();
    static void client_callback(AvahiClient*, AvahiClientState, void*);
    static void browse_callback(AvahiServiceBrowser*, AvahiIfIndex, AvahiProtocol,
                                AvahiBrowserEvent, const char*, const char*, const char*,
                                AvahiLookupResultFlags, void*);
    static void resolve_callback(AvahiServiceResolver*, AvahiIfIndex, AvahiProtocol,
                                 AvahiResolverEvent, const char*, const char*, const char*,
                                 const char*, const AvahiAddress*, uint16_t, AvahiStringList*,
                                 AvahiLookupResultFlags, void*);

    void report_error(const std::string& message);
    std::string get_avahi_error() const;
    void remove_resolver(ResolverId);
    void add_resolver(ResolverId, AvahiServiceResolver*);
    void chromecasts_update(AvahiServiceResolver* resolver, const std::string& name,
                            const asio::ip::tcp::endpoint&,
                            const std::map<std::string, std::string>& dns);
    void chromecasts_remove(AvahiServiceResolver* resolver);
    void send_update(UpdateType, InternalChromecastInfo*) const;

    std::map<std::string, std::string> avahiDNSStringListToMap(AvahiStringList* node);
    asio::ip::tcp::endpoint avahiAddresToAsioEndpoint(const AvahiAddress* address, uint16_t port);

    std::shared_ptr<spdlog::logger> logger;
    asio::io_service& io_service;
    AsioAvahiPoll poll;
    UpdateHandler update_handler = nullptr;
    ErrorHandler error_handler = nullptr;
    AvahiClient* avahi_client = nullptr;
    AvahiServiceBrowser* avahi_browser = nullptr;
    bool stopped;

    std::unordered_map<ResolverId, AvahiServiceResolver*, ResolverIdHash> resolvers;
    std::unordered_map<std::string, std::unique_ptr<InternalChromecastInfo>> chromecasts;
    std::unordered_map<AvahiServiceResolver*, InternalChromecastInfo*> resolver_to_chromecast;
};
