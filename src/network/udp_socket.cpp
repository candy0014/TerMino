#include "tetr/network/udp_socket.h"
#ifdef TETR_PORTABLE_DNS
#include "portable_dns.h"
#endif

#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

#ifdef __linux__
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#endif

namespace Network {
namespace {

constexpr std::size_t kMaxUdpPayload = 65507;

void validate_port(UdpPort port) {
    if (port > 65535U) throw UdpException("UDP port outside 0..65535");
}

#ifdef _WIN32
class WinsockRuntime {
public:
    WinsockRuntime() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw UdpException("WSAStartup failed");
        }
    }
    ~WinsockRuntime() { WSACleanup(); }
};

WinsockRuntime& winsock_runtime() {
    static WinsockRuntime runtime;
    return runtime;
}
#endif

} // namespace

UdpException::UdpException(std::string error):std::runtime_error(std::move(error)){}

UdpSocket::UdpSocket(){
#ifdef _WIN32
    (void)winsock_runtime();
#endif
    create_socket();
}

UdpSocket::~UdpSocket(){close();}

std::string UdpSocket::resolve_ipv4(const std::string& host) {
#ifdef _WIN32
    (void)winsock_runtime();
#endif
    // Reject URLs, ports, whitespace and embedded NULs before using the OS
    // resolver. The online setting accepts an IPv4 address or DNS hostname.
    if (host.empty() || host.size() > 253 ||
        host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::string::npos) {
        throw UdpException("[resolve] invalid server address");
    }
    in_addr numeric{};
#ifdef _WIN32
    if (InetPtonA(AF_INET, host.c_str(), &numeric) == 1) return host;
#else
    if (inet_pton(AF_INET, host.c_str(), &numeric) == 1) return host;
#endif
#ifdef TETR_PORTABLE_DNS
    return Detail::resolve_portable_ipv4(host);
#else
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0) {
        throw UdpException("[resolve] cannot resolve server IPv4 address");
    }
    const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(result, freeaddrinfo);
    for (auto* candidate = result; candidate; candidate = candidate->ai_next) {
        if (candidate->ai_family != AF_INET || !candidate->ai_addr ||
            candidate->ai_addrlen < sizeof(sockaddr_in)) continue;
        const auto* address = reinterpret_cast<const sockaddr_in*>(candidate->ai_addr);
        std::array<char, INET_ADDRSTRLEN> ip{};
#ifdef _WIN32
        if (InetNtopA(AF_INET, &address->sin_addr, ip.data(),
                     static_cast<DWORD>(ip.size())) != nullptr) return ip.data();
#else
        if (inet_ntop(AF_INET, &address->sin_addr, ip.data(), ip.size()) != nullptr) return ip.data();
#endif
    }
    throw UdpException("[resolve] cannot resolve server IPv4 address");
#endif
}

UdpAddress UdpSocket::resolve_address(const std::string& endpoint, UdpPort defaultPort) {
    const auto separator = endpoint.find(':');
    auto port = defaultPort;
    std::string host = endpoint;
    if (separator != std::string::npos) {
        if (endpoint.find(':', separator + 1) != std::string::npos)
            throw UdpException("[resolve] invalid server address");
        host = endpoint.substr(0, separator);
        const auto text = std::string_view(endpoint).substr(separator + 1);
        if (text.empty() || text.size() > 5) throw UdpException("[resolve] invalid server port");
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), port);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw UdpException("[resolve] invalid server port");
    }
    if (port == 0 || port > 65535U) throw UdpException("[resolve] invalid server port");
    return {resolve_ipv4(host), port};
}

void UdpSocket::create_socket() {
    socketId_=socket(AF_INET,SOCK_DGRAM,0);
    if(!valid()) throw UdpException("[constructor] cannot create UDP socket");
    if(nonBlocking_) set_non_blocking();
}

void UdpSocket::set_non_blocking(){
    if(!valid()) throw UdpException("[set_non_blocking] socket is closed");
#ifdef __linux__
    const int flags=fcntl(socketId_,F_GETFL,0);
    if(flags==-1 || fcntl(socketId_,F_SETFL,flags|O_NONBLOCK)==-1) {
        throw UdpException("[set_non_blocking] fcntl failed");
    }
#endif
#ifdef _WIN32
    u_long mode=1;
    if(ioctlsocket(socketId_,FIONBIO,&mode)!=0) {
        throw UdpException("[set_non_blocking] ioctlsocket failed");
    }
#endif
    nonBlocking_=true;
}

void UdpSocket::close() noexcept {
    if(!valid()) return;
#ifdef __linux__
    ::close(socketId_);
#endif
#ifdef _WIN32
    closesocket(socketId_);
#endif
    socketId_=kInvalidSocket;
    bound_=false;
}

void UdpSocket::bind(UdpPort port){
    validate_port(port);
    if(bound_){
        close();
        create_socket();
    }
    if(!valid()) create_socket();
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_ANY);
    address.sin_port=htons(static_cast<std::uint16_t>(port));
    if(::bind(socketId_,reinterpret_cast<sockaddr*>(&address),sizeof(sockaddr_in))==-1){
        std::stringstream error;
        error<<"[bind] cannot bind UDP socket on port="<<port;
        throw UdpException(error.str());
    }
    bound_=true;
}

void UdpSocket::send(const std::string& ip,UdpPort port,const UdpPayload& data){
    validate_port(port);
    if(!valid()) throw UdpException("[send] socket is closed");
    if(data.size()>kMaxUdpPayload) throw UdpException("[send] UDP payload exceeds 65507 bytes");

    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_port=htons(static_cast<std::uint16_t>(port));
#ifdef __linux__
    if(inet_pton(AF_INET,ip.c_str(),&address.sin_addr)!=1) {
        throw UdpException("[send] invalid IPv4 address");
    }
#endif
#ifdef _WIN32
    if(InetPtonA(AF_INET,ip.c_str(),&address.sin_addr)!=1) {
        throw UdpException("[send] invalid IPv4 address");
    }
#endif
    const auto sent=sendto(socketId_,data.data(),static_cast<int>(data.size()),0,
                           reinterpret_cast<sockaddr*>(&address),sizeof(sockaddr_in));
#ifdef _WIN32
    if(sent==SOCKET_ERROR || static_cast<std::size_t>(sent)!=data.size())
#else
    if(sent<0 || static_cast<std::size_t>(sent)!=data.size())
#endif
        throw UdpException("[send] sendto failed");
    bound_=true; // sendto implicitly binds an unbound UDP client socket.
}

void UdpSocket::send(const UdpAddress& address,const UdpPayload& data){
    send(address.ip,address.port,data);
}

std::optional<Datagram> UdpSocket::try_receive(){
    if(!valid()) throw UdpException("[receive] socket is closed");
#ifdef _WIN32
    // Winsock cannot receive on an unbound datagram socket. Match POSIX's
    // empty/timed receive behavior by allocating an ephemeral local endpoint.
    if(!bound_) bind(0);
#endif
    Datagram result{{"",0},""};
    sockaddr_in address{};
#ifdef _WIN32
    int size=sizeof(address);
#else
    socklen_t size=sizeof(address);
#endif
    std::array<char,kMaxUdpPayload> buffer{};
    const auto len=recvfrom(socketId_,buffer.data(),static_cast<int>(buffer.size()),0,
                            reinterpret_cast<sockaddr*>(&address),&size);
#ifdef __linux__
    if(len<0){
        bool wouldBlock=errno==EAGAIN;
#if EWOULDBLOCK != EAGAIN
        wouldBlock=wouldBlock||errno==EWOULDBLOCK;
#endif
        if(wouldBlock) return std::nullopt;
        if(errno==EINTR) return std::nullopt;
        throw UdpException("[receive] recvfrom failed");
    }
#endif
#ifdef _WIN32
    if(len==SOCKET_ERROR){
        const int error=WSAGetLastError();
        // ICMP Port Unreachable may arrive while the relay is starting. UDP
        // has no connection to reset; the connection layer handles retries.
        if(error==WSAEWOULDBLOCK || error==WSAEINTR || error==WSAECONNRESET)
            return std::nullopt;
        throw UdpException("[receive] recvfrom failed: " + std::to_string(error));
    }
#endif
    result.data.assign(buffer.data(),static_cast<std::size_t>(len));
    std::array<char,INET_ADDRSTRLEN> ip{};
#ifdef _WIN32
    if(InetNtopA(AF_INET,&address.sin_addr,ip.data(),static_cast<DWORD>(ip.size()))==nullptr)
#else
    if(inet_ntop(AF_INET,&address.sin_addr,ip.data(),ip.size())==nullptr)
#endif
        throw UdpException("[receive] failed to format peer address");
    result.address.ip=ip.data();
    result.address.port=ntohs(address.sin_port);
    return result;
}

std::optional<Datagram> UdpSocket::receive_for(std::chrono::milliseconds timeout){
    if(timeout.count()<=0) return try_receive();
    if(!valid()) throw UdpException("[receive_for] socket is closed");
#ifdef _WIN32
    if(!bound_) bind(0);
#endif

    const auto deadline=std::chrono::steady_clock::now()+timeout;
    for(;;){
        const auto now=std::chrono::steady_clock::now();
        if(now>=deadline) return std::nullopt;
        const auto remaining=std::chrono::duration_cast<std::chrono::microseconds>(deadline-now);

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketId_,&readSet);
        timeval tv{};
        tv.tv_sec=static_cast<long>(remaining.count()/1000000);
        tv.tv_usec=static_cast<long>(remaining.count()%1000000);
#ifdef _WIN32
        const int ready=select(0,&readSet,nullptr,nullptr,&tv);
#else
        const int ready=select(socketId_+1,&readSet,nullptr,nullptr,&tv);
#endif
        if(ready>0){
            if(auto datagram=try_receive()) return datagram;
            continue;
        }
        if(ready==0) return std::nullopt;
#ifdef __linux__
        if(errno==EINTR) continue;
#endif
#ifdef _WIN32
        if(WSAGetLastError()==WSAEINTR) continue;
#endif
        throw UdpException("[receive_for] select failed");
    }
}

Datagram UdpSocket::receive_blocking(){
    if(!valid()) throw UdpException("[receive_blocking] socket is closed");
#ifdef _WIN32
    if(!bound_) bind(0);
#endif
    for(;;){
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketId_,&readSet);
#ifdef _WIN32
        const int ready=select(0,&readSet,nullptr,nullptr,nullptr);
#else
        const int ready=select(socketId_+1,&readSet,nullptr,nullptr,nullptr);
#endif
        if(ready>0){
            if(auto datagram=try_receive()) return std::move(*datagram);
            continue;
        }
#ifdef __linux__
        if(ready<0 && errno==EINTR) continue;
#endif
#ifdef _WIN32
        if(ready==SOCKET_ERROR && WSAGetLastError()==WSAEINTR) continue;
#endif
        throw UdpException("[receive_blocking] select failed");
    }
}

} // namespace Network
