#include "curl_runtime.h"
#include "tetr/network/udp_socket.h"

#include <curl/curl.h>

namespace Network::Detail {
struct CurlRuntime {
    CurlRuntime() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw UdpException("WebSocket initialization failed.");
    }
    ~CurlRuntime() { curl_global_cleanup(); }
};

std::shared_ptr<CurlRuntime> initialize_curl() {
    // curl also initializes c-ares. Share one lifetime with UDP resolution,
    // rather than racing independent c-ares init/cleanup calls with workers.
    static auto runtime = std::make_shared<CurlRuntime>();
    return runtime;
}
} // namespace Network::Detail
