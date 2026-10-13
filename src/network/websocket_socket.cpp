#include "tetr/network/websocket_socket.h"
#include "tetr/network/udp_socket.h"

#include <curl/curl.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>

namespace Network {
namespace {
constexpr std::size_t maxMessage = 65507;
constexpr std::size_t maxBuffered = 2 * 1024 * 1024;
constexpr std::size_t maxQueuedMessages = 1024;

struct CurlRuntime {
    CurlRuntime() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw UdpException("WebSocket initialization failed.");
    }
    ~CurlRuntime() { curl_global_cleanup(); }
};
std::shared_ptr<CurlRuntime> initialize_curl() {
    static auto runtime = std::make_shared<CurlRuntime>();
    return runtime;
}
}

struct WebSocketSocket::Impl {
    // Keep curl alive until this socket's worker has stopped, even when the
    // application owns sockets with static storage duration.
    std::shared_ptr<CurlRuntime> runtime;
    std::string url;
    std::atomic<bool> stopping{false};
    std::mutex mutex;
    std::condition_variable available;
    std::deque<std::string> outgoing, incoming;
    std::size_t outgoingBytes = 0, incomingBytes = 0;
    std::string error;
    std::thread worker;

    explicit Impl(std::string address) : runtime(initialize_curl()), url(std::move(address)) {
        worker = std::thread([this] { run(); });
    }
    ~Impl() { stop(); }
    void stop() noexcept {
        stopping = true;
        available.notify_all();
        if (worker.joinable()) worker.join();
    }
    static int progress(void* self, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        return static_cast<Impl*>(self)->stopping ? 1 : 0;
    }
    void fail(const char* message) {
        std::lock_guard<std::mutex> lock(mutex);
        error = message;
        available.notify_all();
    }
    void run() noexcept {
        try {
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
            if (!curl) throw UdpException("WebSocket initialization failed.");
            curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "TerMino");
            curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "ws,wss");
            curl_easy_setopt(curl.get(), CURLOPT_CONNECT_ONLY, 2L);
            curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
            curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 3500L);
            curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 4500L);
            curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
            curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, this);
            // System trust store on Windows (Schannel); standard CA files on
            // Linux also work with the statically linked portable TLS backend.
#ifdef __linux__
            for (const auto* path : {"/etc/ssl/certs/ca-certificates.crt", "/etc/ssl/cert.pem"}) {
                if (std::filesystem::exists(path)) {
                    curl_easy_setopt(curl.get(), CURLOPT_CAINFO, path);
                    break;
                }
            }
#endif
            const auto connected = curl_easy_perform(curl.get());
            if (stopping) return;
            if (connected != CURLE_OK) {
                fail(connected == CURLE_PEER_FAILED_VERIFICATION
                    ? "WebSocket TLS verification failed." : "Cannot connect to WebSocket server.");
                return;
            }
            std::string pending, received;
            std::size_t sentOffset = 0;
            bool hasPending = false;
            while (!stopping) {
                for (int burst = 0; burst < 32; ++burst) {
                    if (!hasPending) {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (outgoing.empty()) break;
                        pending = std::move(outgoing.front()); outgoing.pop_front();
                        sentOffset = 0; hasPending = true;
                    }
                    std::size_t sent = 0;
                    const auto result = curl_ws_send(curl.get(), pending.data() + sentOffset,
                        pending.size() - sentOffset, &sent, 0, CURLWS_BINARY);
                    sentOffset += sent;
                    if (result != CURLE_OK && result != CURLE_AGAIN)
                        throw UdpException("WebSocket server disconnected.");
                    if (sentOffset == pending.size() && result == CURLE_OK) {
                        std::lock_guard<std::mutex> lock(mutex);
                        outgoingBytes -= pending.size();
                        hasPending = false;
                    } else break;
                }
                for (int burst = 0; burst < 64; ++burst) {
                    std::array<char, 8192> buffer{};
                    std::size_t size = 0;
                    const curl_ws_frame* metadata = nullptr;
                    const auto result = curl_ws_recv(curl.get(), buffer.data(), buffer.size(), &size, &metadata);
                    if (result == CURLE_AGAIN) break;
                    if (result != CURLE_OK || !metadata || (metadata->flags & CURLWS_CLOSE))
                        throw UdpException("WebSocket server disconnected.");
                    if (metadata->flags & (CURLWS_PING | CURLWS_PONG)) continue;
                    if (!(metadata->flags & (CURLWS_BINARY | CURLWS_TEXT)) ||
                        received.size() + size > maxMessage || metadata->bytesleft > static_cast<curl_off_t>(maxMessage))
                        throw UdpException("Invalid WebSocket message.");
                    received.append(buffer.data(), size);
                    if (metadata->bytesleft == 0 && !(metadata->flags & CURLWS_CONT)) {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (incomingBytes + received.size() > maxBuffered || incoming.size() >= maxQueuedMessages)
                            throw UdpException("Too much pending WebSocket data.");
                        incomingBytes += received.size(); incoming.push_back(std::move(received));
                        received.clear(); available.notify_all();
                    }
                }
                // Bounded sleep avoids busy-spinning when sockets are idle or
                // backpressured. Enqueue and stop can wake the worker early.
                std::unique_lock<std::mutex> lock(mutex);
                available.wait_for(lock, std::chrono::milliseconds(10));
            }
        } catch (const std::exception& exception) {
            if (!stopping) fail(exception.what());
        } catch (...) {
            if (!stopping) fail("WebSocket initialization failed.");
        }
    }
};

bool WebSocketSocket::recognizes(const std::string& address) noexcept {
    return address.rfind("ws://", 0) == 0 || address.rfind("wss://", 0) == 0;
}
WebSocketSocket::WebSocketSocket(std::string url) {
    if (!recognizes(url) || url.size() > 2048 ||
        url.find_first_of("\r\n\t ") != std::string::npos ||
        url.find('\0') != std::string::npos)
        throw UdpException("Invalid WebSocket URL.");
    impl_ = std::make_unique<Impl>(std::move(url));
}
WebSocketSocket::~WebSocketSocket() = default;
void WebSocketSocket::close() noexcept { impl_->stop(); }
void WebSocketSocket::send(const std::string& message) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->error.empty()) throw UdpException(impl_->error);
    if (impl_->stopping) throw UdpException("WebSocket server disconnected.");
    if (message.size() > maxMessage) throw UdpException("Invalid WebSocket message.");
    if (impl_->outgoingBytes + message.size() > maxBuffered || impl_->outgoing.size() >= maxQueuedMessages)
        throw UdpException("Too much pending WebSocket data.");
    impl_->outgoingBytes += message.size(); impl_->outgoing.push_back(message);
    impl_->available.notify_all();
}
std::optional<std::string> WebSocketSocket::receive_for(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->available.wait_for(lock, timeout, [this] {
        return !impl_->incoming.empty() || !impl_->error.empty() || impl_->stopping;
    });
    if (!impl_->incoming.empty()) {
        auto message = std::move(impl_->incoming.front()); impl_->incoming.pop_front();
        impl_->incomingBytes -= message.size(); return message;
    }
    if (!impl_->error.empty()) throw UdpException(impl_->error);
    if (impl_->stopping) throw UdpException("WebSocket server disconnected.");
    return std::nullopt;
}
} // namespace Network
