#ifndef TETR_NETWORK_WEBSOCKET_SOCKET_H
#define TETR_NETWORK_WEBSOCKET_SOCKET_H

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace Network {

// Bounded event batches with one-packet legacy fallback. TLS belongs to libcurl;
// all I/O runs on one worker, keeping DNS/connect/send away from the game loop.
class WebSocketSocket {
public:
    explicit WebSocketSocket(std::string url);
    ~WebSocketSocket();
    WebSocketSocket(const WebSocketSocket&) = delete;
    WebSocketSocket& operator=(const WebSocketSocket&) = delete;
    void send(const std::string& message);
    std::optional<std::string> receive_for(std::chrono::milliseconds timeout);
    void close() noexcept;
    bool uses_event_stream() const noexcept;
    static bool recognizes(const std::string& address) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Network
#endif
