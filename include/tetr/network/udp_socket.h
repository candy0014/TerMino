#ifndef TETR_NETWORK_UDP_SOCKET_H
#define TETR_NETWORK_UDP_SOCKET_H

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>

#ifdef __linux__
#include <netinet/in.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#endif

namespace Network {

using UdpPort = unsigned int;
using UdpPayload = std::string;

struct UdpAddress {
    std::string ip;
    UdpPort port = 0;
};

struct Datagram {
    UdpAddress address;
    UdpPayload data;
};

class UdpException : public std::runtime_error {
public:
    explicit UdpException(std::string error);
};

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Resolve once when connecting; send() continues to use numeric IPv4.
    static std::string resolve_ipv4(const std::string& host);
    static UdpAddress resolve_address(const std::string& endpoint, UdpPort defaultPort);
    void close() noexcept;
    void bind(UdpPort port);
    void send(const std::string& ip, UdpPort port, const UdpPayload& data);
    void send(const UdpAddress& address, const UdpPayload& data);
    void set_non_blocking();
    std::optional<Datagram> try_receive();
    std::optional<Datagram> receive_for(std::chrono::milliseconds timeout);
    Datagram receive_blocking();

private:
#ifdef _WIN32
    using NativeSocket = SOCKET;
    static constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
    using NativeSocket = int;
    static constexpr NativeSocket kInvalidSocket = -1;
#endif

    void create_socket();
    bool valid() const noexcept { return socketId_ != kInvalidSocket; }

    NativeSocket socketId_ = kInvalidSocket;
    bool bound_ = false;
    bool nonBlocking_ = false;
};

} // namespace Network

#endif
