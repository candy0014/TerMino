#include "portable_dns.h"
#include "curl_runtime.h"
#include "tetr/network/udp_socket.h"

#include <ares.h>
#include <arpa/inet.h>
#include <array>
#include <memory>

namespace Network::Detail {
namespace {
struct Lookup {
    int status = ARES_ENOTFOUND;
    bool found = false;
    in_addr address{};
};

void completed(void* data, int status, int, ares_addrinfo* addresses) noexcept {
    auto& lookup = *static_cast<Lookup*>(data);
    lookup.status = status;
    if (status == ARES_SUCCESS && addresses) {
        for (auto* candidate = addresses->nodes; candidate; candidate = candidate->ai_next) {
            if (candidate->ai_family != AF_INET || !candidate->ai_addr ||
                candidate->ai_addrlen < sizeof(sockaddr_in)) continue;
            lookup.address = reinterpret_cast<const sockaddr_in*>(candidate->ai_addr)->sin_addr;
            lookup.found = true;
            break;
        }
    }
    if (addresses) ares_freeaddrinfo(addresses);
}
} // namespace

std::string resolve_portable_ipv4(const std::string& host) {
    const auto runtime = initialize_curl();
    // Keep callback storage alive while destruction cancels pending lookups.
    Lookup lookup;
    ares_channel_t* raw = nullptr;
    ares_options options{};
    options.evsys = ARES_EVSYS_DEFAULT;
    options.timeout = 1500;
    options.tries = 2;
    char lookups[] = "fb"; // /etc/hosts, then DNS; no external NSS modules.
    options.lookups = lookups;
    if (ares_init_options(&raw, &options,
            ARES_OPT_EVENT_THREAD | ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES | ARES_OPT_LOOKUPS) != ARES_SUCCESS)
        throw UdpException("[resolve] cannot resolve server IPv4 address");
    std::unique_ptr<ares_channel_t, decltype(&ares_destroy)> channel(raw, ares_destroy);
    ares_addrinfo_hints hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = ARES_AI_NOSORT;
    ares_getaddrinfo(channel.get(), host.c_str(), nullptr, &hints, completed, &lookup);
    const auto waited = ares_queue_wait_empty(channel.get(), 3500);
    // Join the event worker before inspecting callback output. This also
    // completes cancellation callbacks on timeouts before storage disappears.
    channel.reset();
    if (waited != ARES_SUCCESS || lookup.status != ARES_SUCCESS || !lookup.found)
        throw UdpException("[resolve] cannot resolve server IPv4 address");
    std::array<char, INET_ADDRSTRLEN> text{};
    if (!inet_ntop(AF_INET, &lookup.address, text.data(), text.size()))
        throw UdpException("[resolve] cannot resolve server IPv4 address");
    return text.data();
}
} // namespace Network::Detail
