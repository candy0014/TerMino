#ifndef TETR_NETWORK_PRIVATE_PORTABLE_DNS_H
#define TETR_NETWORK_PRIVATE_PORTABLE_DNS_H

#include <string>

namespace Network::Detail {
std::string resolve_portable_ipv4(const std::string& host);
} // namespace Network::Detail

#endif
