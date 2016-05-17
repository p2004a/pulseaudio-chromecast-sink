#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include "asio_avahi_poll.h"
#include "util.h"

class ChromecastFinder {
  public:
    enum class UpdateType { NEW, UPDATE, REMOVE };

    struct ChromecastInfo {
        std::string name;
        std::set<boost::asio::ip::tcp::endpoint> endpoints;
        std::map<std::string, std::string> dns;
    };

    ChromecastFinder(boost::asio::io_service& io_service_,
                     std::function<void(UpdateType, ChromecastInfo)> update_callback_);

    ~ChromecastFinder();

    void stop();

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
        std::map<boost::asio::ip::tcp::endpoint, int> endpoint_count;
        std::map<AvahiServiceResolver*, boost::asio::ip::tcp::endpoint> endpoints;
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

    std::string get_avahi_error() const;
    void remove_resolver(ResolverId);
    void add_resolver(ResolverId, AvahiServiceResolver*);
    void chromecasts_update(AvahiServiceResolver* resolver, const std::string& name,
                            const boost::asio::ip::tcp::endpoint&,
                            const std::map<std::string, std::string>& dns);
    void chromecasts_remove(AvahiServiceResolver* resolver);
    void send_update(UpdateType, InternalChromecastInfo*) const;

    boost::asio::io_service& io_service;
    AsioAvahiPoll poll;
    std::function<void(UpdateType, ChromecastInfo)> update_callback;
    AvahiClient* avahi_client = nullptr;
    AvahiServiceBrowser* avahi_browser = nullptr;
    bool stopped;

    std::unordered_map<ResolverId, AvahiServiceResolver*, ResolverIdHash> resolvers;
    std::unordered_map<std::string, std::unique_ptr<InternalChromecastInfo>> chromecasts;
    std::unordered_map<AvahiServiceResolver*, InternalChromecastInfo*> resolver_to_chromecast;
};
