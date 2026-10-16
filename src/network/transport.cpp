#include "tetr/network/transport.h"
#include "tetr/network/network_services.h"

#include <chrono>
#include <stdexcept>
#include <algorithm>

namespace Network {
void send(NetworkServices& services, const std::string& message) {
    if (uses_local_duplex(services)) {
        auto& transport = *services.localTransport;
        {
            std::lock_guard<std::mutex> lock(transport.mutex);
            transport.inbox[1 - services.localEndpoint].push_back(message);
        }
        transport.available.notify_all();
        return;
    }
    // Sending local input must not drain or decode the opponent's backlog.
    // Frame/lobby polling owns reception and heartbeat work.
    if(services.connection!=ConnectionState::Connected) poll_connection(services);
    if (services.connection != ConnectionState::Connected)
        throw ConnectionError(services.connectionMessage);
    send_online(services, services.multiplayerRoom?encode_match_packet(services.matchEpoch,message):message);
}

std::optional<std::string> try_receive(NetworkServices& services) {
    if (uses_local_duplex(services)) {
        auto& transport = *services.localTransport;
        std::lock_guard<std::mutex> lock(transport.mutex);
        auto& inbox = transport.inbox[services.localEndpoint];
        if (inbox.empty()) return std::nullopt;
        std::string message = std::move(inbox.front());
        inbox.pop_front();
        return message;
    }
    poll_connection(services);
    if (services.connection != ConnectionState::Connected && services.connection != ConnectionState::Connecting)
        throw ConnectionError(services.connectionMessage);
    if (services.onlineInbox.empty()) return std::nullopt;
    auto message = std::move(services.onlineInbox.front());
    services.onlineInbox.pop_front();
    return message;
}

std::optional<std::string> receive_for(NetworkServices& services, std::chrono::milliseconds timeout) {
    if (uses_local_duplex(services)) {
        auto& transport = *services.localTransport;
        std::unique_lock<std::mutex> lock(transport.mutex);
        auto& inbox = transport.inbox[services.localEndpoint];
        if (timeout.count() > 0)
            transport.available.wait_for(lock, timeout, [&inbox] { return !inbox.empty(); });
        if (inbox.empty()) return std::nullopt;
        std::string message = std::move(inbox.front());
        inbox.pop_front();
        return message;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (auto message = try_receive(services)) return message;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) return std::nullopt;
        poll_connection(services, std::min(remaining, std::chrono::milliseconds(10)));
    } while (true);
}

std::string receive_blocking(NetworkServices& services) {
    if (uses_local_duplex(services)) {
        auto& transport = *services.localTransport;
        std::unique_lock<std::mutex> lock(transport.mutex);
        auto& inbox = transport.inbox[services.localEndpoint];
        transport.available.wait(lock, [&inbox] { return !inbox.empty(); });
        std::string message = std::move(inbox.front());
        inbox.pop_front();
        return message;
    }
    for (;;) if (auto message = receive_for(services, std::chrono::milliseconds(10))) return *message;
}

void drain(NetworkServices& services) {
    while (try_receive(services)) {}
}

} // namespace Network
