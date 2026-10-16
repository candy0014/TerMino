#ifndef TETR_NETWORK_PRIVATE_CURL_RUNTIME_H
#define TETR_NETWORK_PRIVATE_CURL_RUNTIME_H

#include <memory>

namespace Network::Detail {
struct CurlRuntime;
std::shared_ptr<CurlRuntime> initialize_curl();
} // namespace Network::Detail

#endif
