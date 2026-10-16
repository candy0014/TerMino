#include "tetr/network/udp_socket.h"
#include "tetr/network/network_services.h"
#include "tetr/network/transport.h"

#include <cassert>
#include <chrono>
#include <string>

int main() {
    assert(Network::UdpSocket::resolve_ipv4("127.0.0.1")=="127.0.0.1");
    assert(Network::UdpSocket::resolve_ipv4("localhost")=="127.0.0.1");
    const auto defaultAddress=Network::UdpSocket::resolve_address("127.0.0.1",3000);
    assert(defaultAddress.ip=="127.0.0.1" && defaultAddress.port==3000);
    const auto customAddress=Network::UdpSocket::resolve_address("localhost:43210",3000);
    assert(customAddress.ip=="127.0.0.1" && customAddress.port==43210);
    for (const auto* endpoint : {"localhost:","localhost:0","localhost:65536",
                                "localhost:-1","localhost:abc","localhost:3000/path",
                                ":3000","::1","https://localhost:3000"}) {
        bool rejected=false;
        try { (void)Network::UdpSocket::resolve_address(endpoint,3000); }
        catch(const Network::UdpException&) { rejected=true; }
        assert(rejected);
    }
    for (const auto& host : {std::string{}, std::string{"https://game.example.com"},
                            std::string{"game.example.com:3000"}, std::string{"::1"},
                            std::string{"bad host"}, std::string{"localhost\0evil",14}}) {
        bool rejected=false;
        try { (void)Network::UdpSocket::resolve_ipv4(host); }
        catch(const Network::UdpException&) { rejected=true; }
        assert(rejected);
    }
    {
        Network::NetworkServices first;
        Network::NetworkServices second;
        Network::connect_local_duplex(first, second);
        assert(Network::uses_local_duplex(first));
        assert(Network::uses_local_duplex(second));
        Network::send(first, "first-to-second");
        assert(Network::receive_for(second, std::chrono::milliseconds(1)) ==
               std::optional<std::string>{"first-to-second"});
        Network::send(second, "second-to-first");
        assert(Network::receive_blocking(first) == "second-to-first");
        assert(!Network::try_receive(first));
        assert(!Network::receive_for(first,std::chrono::milliseconds(0)));
        assert(!Network::receive_for(first,std::chrono::milliseconds(-1)));
        Network::send(second,"queued-poll");
        assert(Network::receive_for(first,std::chrono::milliseconds(0))=="queued-poll");
        Network::disconnect_local_duplex(first);
        Network::disconnect_local_duplex(second);
        assert(!Network::uses_local_duplex(first));
        assert(!Network::uses_local_duplex(second));
    }

    Network::UdpSocket socket;
    socket.set_non_blocking();

    const auto immediateStart = std::chrono::steady_clock::now();
    const auto immediate = socket.receive_for(std::chrono::milliseconds(0));
    const auto immediateElapsed = std::chrono::steady_clock::now() - immediateStart;
    assert(!immediate);
    assert(immediateElapsed < std::chrono::milliseconds(100));

    const auto start = std::chrono::steady_clock::now();
    const auto result = socket.receive_for(std::chrono::milliseconds(20));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(!result);
    // This guards against accidentally regressing receive_for() back to a
    // non-blocking try_receive() loop. Scheduling can make the wait longer,
    // but it must not return substantially before its requested deadline.
    assert(elapsed >= std::chrono::milliseconds(10));
    assert(elapsed < std::chrono::seconds(2));

    Network::UdpSocket receiver;
    receiver.set_non_blocking();
    unsigned port=0;
    for(unsigned candidate=43000; candidate<43100; ++candidate){
        try { receiver.bind(candidate); port=candidate; break; }
        catch(const Network::UdpException&) {}
    }
    assert(port!=0);

    Network::UdpSocket sender;
    const std::string payload(32*1024, 'x');
    sender.send("127.0.0.1",port,payload);
    const auto datagram=receiver.receive_for(std::chrono::milliseconds(500));
    assert(datagram);
    assert(datagram->data==payload);
}
