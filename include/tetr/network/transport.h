#ifndef TETR_NETWORK_TRANSPORT_H
#define TETR_NETWORK_TRANSPORT_H

#include <chrono>
#include <optional>
#include <string>

namespace Network { struct NetworkServices; }

namespace Network {

// Raw UDP transport boundary.  Game/Engine code should use a typed channel
// instead of depending on these string primitives directly.
void send(NetworkServices& services, const std::string& message);
std::optional<std::string> try_receive(NetworkServices& services);
std::optional<std::string> receive_for(NetworkServices& services, std::chrono::milliseconds timeout);
std::string receive_blocking(NetworkServices& services);
void drain(NetworkServices& services);

} // namespace Network

#endif
