#include "tetr/network/relay_server.h"
#include "tetr/network/network_services.h"
#include "tetr/network/transport.h"
#include "tetr/runtime/game_context.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

struct Client {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Network::NetworkServices network;
    Client() { context.attach_network(network); }
    void join(const std::string& ip="127.0.0.1") { Network::initialize_network_services(context,ip,3); }
};
int main() {
    using Network::ConnectionState;
    Client a,b,c;
    a.join(); // Client is allowed to start before its server.
    assert(a.network.connection==ConnectionState::Connecting);
    Network::RelayServer server(123);
    server.bind(3000);
    a.network.lastConnectionSend={};
    auto pump=[&](int count=40) {
        for(int i=0;i<count;++i) {
            server.poll(std::chrono::milliseconds(1));
            for(auto* client:{&a,&b,&c}) Network::poll_connection(client->network);
        }
    };
    const auto connectingRevision=a.network.roomRevision;
    pump(); assert(a.network.connection==ConnectionState::Connected);
    assert(a.network.roomRevision>connectingRevision); // Connection completion refreshes settings.
    a.network.lastConnectionSend={};Network::poll_connection(a.network);
    for(int i=0;i<10;++i) server.poll(std::chrono::milliseconds(1));
    a.network.lastConnectionReply=std::chrono::steady_clock::now()-Network::kConnectedTimeout-std::chrono::seconds(1);
    Network::poll_connection(a.network);
    assert(a.network.connection==ConnectionState::Connected); // Consume queued pong before timeout.
    b.join("localhost"); pump(); assert(b.network.connection==ConnectionState::Connected);
    assert(b.network.serverIp=="localhost");
    assert(b.network.resolvedServerIp=="127.0.0.1");
    c.join(); pump(); assert(c.network.connection==ConnectionState::Connected);
    for (auto* client : {&a,&b,&c}) assert(client->network.room.name.empty() && client->network.onlineInbox.empty());
    Network::request_room(a.network,Network::RoomOperation::Create,"TEST"); pump();
    Network::request_room(b.network,Network::RoomOperation::Join,"TEST"); pump();
    Network::request_room(c.network,Network::RoomOperation::Join,"TEST"); pump();
    assert(c.network.room.name.empty() && c.network.roomMessage.find("full")!=std::string::npos);
    Network::set_room_policy(a.network,{false}); pump();
    for(auto* client:{&a,&b})
        Network::send(client->network,std::string(Network::encode_client_lobby_signal(Network::ClientLobbySignal::ReadyForConfig)));
    pump();
    Network::LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;
    setup.turnPieces=1; setup.simulationPps=0;
    for(auto* client:{&a,&b}) Network::send(client->network,Network::encode_lobby_player_setup(setup));
    pump();
    for(auto signal:{Network::ClientLobbySignal::SetupReceived,Network::ClientLobbySignal::CountdownComplete}) {
        for(auto* client:{&a,&b}) Network::send(client->network,std::string(Network::encode_client_lobby_signal(signal)));
        pump();
    }
    // Probes work during a full, active round without resetting it or joining.
    Network::UdpSocket probe;
    probe.set_non_blocking();
    probe.send("127.0.0.1",3000,"!probe");
    pump();
    const auto probeReply=probe.receive_for(std::chrono::milliseconds(500));
    assert(probeReply && probeReply->data=="!probe-ok");
    Network::send(a.network,"gameplay-from-ip"); pump();
    Network::send(b.network,"gameplay-from-hostname"); pump();
    bool fromIp=false,fromHostname=false;
    while(auto wire=Network::try_receive(b.network)) if(*wire=="gameplay-from-ip") fromIp=true;
    while(auto wire=Network::try_receive(a.network)) if(*wire=="gameplay-from-hostname") fromHostname=true;
    assert(fromIp && fromHostname);
    Network::request_room(c.network,Network::RoomOperation::Join,"TEST"); pump();
    assert(c.network.room.name.empty()); // A full room also rejects mid-round admission.
    Network::disconnect_online(a.network); pump();
    assert(a.network.connection==ConnectionState::Disconnected);
    bool opponentLeft=false;
    while(auto wire=Network::try_receive(b.network)) if(*wire=="end2") opponentLeft=true;
    assert(opponentLeft);
    Network::request_room(c.network,Network::RoomOperation::Join,"TEST"); pump(); assert(c.network.room.name=="TEST");
    b.join("localhost"); pump(); assert(b.network.connection==ConnectionState::Connected); // Reconnect returns to room selection.
    assert(b.network.room.name.empty());
    Network::request_room(b.network,Network::RoomOperation::Join,"TEST"); pump();
    a.join(); pump(); assert(a.network.connection==ConnectionState::Connected);
    Network::request_room(a.network,Network::RoomOperation::Join,"TEST"); pump(); assert(a.network.room.name.empty());
    Network::disconnect_online(c.network); pump();
    a.join("invalid host"); assert(a.network.connection==ConnectionState::Failed);
    assert(!a.network.initialized && !a.network.socket);
    a.join(); pump(); assert(a.network.connection==ConnectionState::Connected);
    Network::request_room(a.network,Network::RoomOperation::Join,"TEST"); pump(); assert(a.network.room.name=="TEST");
    // A crashed client stops heartbeats; the remaining client keeps its slot.
    const auto deadline=std::chrono::steady_clock::now()+Network::kConnectedTimeout+std::chrono::milliseconds(200);
    while(std::chrono::steady_clock::now()<deadline) {
        server.poll(std::chrono::milliseconds(10));
        Network::poll_connection(b.network);
    }
    c.join(); pump();
    Network::request_room(c.network,Network::RoomOperation::Join,"TEST"); pump(); assert(c.network.room.name=="TEST");
    assert(b.network.room.players==2);
    Network::disconnect_online(a.network); // Expired client's leave must not evict another slot.
    Network::disconnect_online(b.network);
    Network::disconnect_online(c.network);
    pump();
    // A tunnel's public UDP port may differ from the relay's local 3000.
    Network::RelayServer customPortServer(456);
    customPortServer.bind(3001);
    auto customPump=[&]() {
        for(int i=0;i<40;++i) {
            customPortServer.poll(std::chrono::milliseconds(1));
            Network::poll_connection(a.network);
            Network::poll_connection(b.network);
        }
    };
    a.join("127.0.0.1:3001"); b.join("localhost:3001"); customPump();
    Network::request_room(a.network,Network::RoomOperation::Create,"PORT"); customPump();
    Network::request_room(b.network,Network::RoomOperation::Join,"PORT"); customPump();
    Network::set_room_policy(a.network,{false}); customPump();
    for(auto* client:{&a,&b}) {
        assert(client->network.connection==ConnectionState::Connected);
        assert(client->network.serverPort==3001);
        Network::send(client->network,std::string(Network::encode_client_lobby_signal(Network::ClientLobbySignal::ReadyForConfig)));
    }
    customPump();
    for(auto* client:{&a,&b}) Network::send(client->network,Network::encode_lobby_player_setup(setup));
    customPump();
    for(auto signal:{Network::ClientLobbySignal::SetupReceived,Network::ClientLobbySignal::CountdownComplete}) {
        for(auto* client:{&a,&b}) Network::send(client->network,std::string(Network::encode_client_lobby_signal(signal)));
        customPump();
    }
    Network::send(b.network,"tunnel-gameplay"); customPump();
    bool tunnelGameplay=false;
    while(auto wire=Network::try_receive(a.network)) if(*wire=="tunnel-gameplay") tunnelGameplay=true;
    assert(tunnelGameplay);
    Network::disconnect_online(a.network); customPump();
    bool tunnelLeave=false;
    while(auto wire=Network::try_receive(b.network)) if(*wire=="end2") tunnelLeave=true;
    assert(tunnelLeave);
    b.join("localhost:3001"); customPump();
    assert(b.network.connection==ConnectionState::Connected);
    Network::disconnect_online(b.network); customPump();
    std::cerr<<"network_connection_test: OK\n";
}
