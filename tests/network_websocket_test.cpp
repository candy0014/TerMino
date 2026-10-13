#include "tetr/network/network_services.h"
#include "tetr/network/lobby_protocol.h"
#include "tetr/network/transport.h"
#include "tetr/runtime/game_context.h"
#include "support/network_gameplay_frames.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

struct Client {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session, services};
    Network::NetworkServices network;
    Client() { context.attach_network(network); }
    void join(const std::string& address) { Network::initialize_network_services(context, address); }
    ~Client() { Network::disconnect_online(network); }
};

int main(int argc, char** argv) {
    assert(argc >= 2);
    const std::string url = argv[1];
    using namespace std::chrono_literals;
    using Network::ConnectionState;
    if (argc == 3 && std::string(argv[2]) == "--probe") {
        Network::WebSocketSocket probe(url);
        probe.send("!probe");
        assert(probe.receive_for(5s) == "!probe-ok");
        std::cerr << "WebSocket public probe: OK\n";
        return 0;
    }
    if (argc == 3 && std::string(argv[2]) == "--echo") {
        Network::WebSocketSocket socket(url);
        // Larger than curl's receive buffer, includes binary NUL bytes; the
        // test server returns several fragments with a control ping between.
        const std::string payload(60000, '\0');
        socket.send(payload);
        assert(socket.receive_for(5s) == payload);
        socket.send("after-ping");
        assert(socket.receive_for(5s) == "after-ping");
        socket.send("close-now");
        bool closed = false;
        try { socket.receive_for(5s); }
        catch (const Network::UdpException&) { closed = true; }
        assert(closed);
        std::cerr << "WebSocket fragmentation, binary data, ping and close: OK\n";
        return 0;
    }
    if (argc == 3 && std::string(argv[2]) == "--bad-tls") {
        Network::WebSocketSocket socket(url);
        socket.send("!probe");
        bool rejected = false;
        try { socket.receive_for(5s); }
        catch (const Network::UdpException& e) {
            rejected = std::string(e.what()) == "WebSocket TLS verification failed.";
        }
        assert(rejected);
        return 0;
    }
    const std::string roomName = "WebSocket测试-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Client a, b, c;
    int waitNumber=0;
    auto wait = [&](const auto& predicate) {
        ++waitNumber;
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        do {
            for (auto* client : {&a, &b, &c}) Network::poll_connection(client->network);
            if (predicate()) return;
            std::this_thread::sleep_for(5ms);
        } while (std::chrono::steady_clock::now() < deadline);
        std::cerr<<"Timed out at wait "<<waitNumber<<'\n';
        for (auto* client : {&a, &b, &c}) {
            const auto& n=client->network;
            std::cerr<<n.connectionMessage<<" room="<<n.room.name<<" instance="<<n.roomInstance<<" members="<<n.roomMembers.size()<<" inbox="<<n.onlineInbox.size()<<'\n';
            for (const auto& wire:n.onlineInbox) if (wire.size()<50) std::cerr<<"  "<<wire<<'\n';
        }
        assert(false && "WebSocket integration timed out");
    };
    auto contains = [](const Client& client, const std::string& wire) {
        const auto& inbox = client.network.onlineInbox;
        return std::find(inbox.begin(), inbox.end(), wire) != inbox.end();
    };
    auto waitSignal = [&](Network::ServerLobbySignal signal) {
        const auto wire = std::string(Network::encode_server_lobby_signal(signal));
        wait([&] { return contains(a, wire) && contains(b, wire); });
    };
    auto sendSignal = [&](Network::ClientLobbySignal signal) {
        const auto wire = std::string(Network::encode_client_lobby_signal(signal));
        for (auto* client : {&a, &b}) Network::send(client->network, wire);
    };
    a.join(url); b.join(url);
    wait([&] { return a.network.connection == ConnectionState::Connected &&
                    b.network.connection == ConnectionState::Connected; });
    assert(a.network.webSocket && !a.network.socket && b.network.webSocket && !b.network.socket);
    assert(a.network.room.name.empty() && b.network.room.name.empty());
    assert(a.network.onlineInbox.empty() && b.network.onlineInbox.empty());
    Network::request_room(a.network, Network::RoomOperation::Create, roomName, false);
    wait([&] { return a.network.room.name == roomName; });
    Network::request_room(b.network, Network::RoomOperation::Join, roomName);
    wait([&] { return b.network.room.name == roomName &&
                    a.network.roomMembers.size()==2 && b.network.roomMembers.size()==2; });
    assert(a.network.roomMembers.size()==2 && a.network.roomMembers[0].host);
    Network::set_room_policy(a.network,{false,false});
    wait([&] {return !a.network.pendingRoomRequest && !a.network.roomPolicy.forceRules && !b.network.roomPolicy.forceRules;});
    Network::set_online_username(a.network,"公网房主");
    wait([&] { return a.network.confirmedUsername == "公网房主" && b.network.roomMembers[0].username == "公网房主"; });
    waitSignal(Network::ServerLobbySignal::PairReady);
    c.join(url);
    wait([&] { return c.network.connection == ConnectionState::Connected; });
    Network::request_room(c.network, Network::RoomOperation::List);
    wait([&] { return !c.network.pendingRoomRequest; });
    for (const auto& room : c.network.publicRooms) assert(room.name != roomName); // Private rooms stay hidden.
    Network::request_room(c.network, Network::RoomOperation::Join, roomName + "-missing");
    wait([&] { return !c.network.pendingRoomRequest; });
    assert(c.network.roomMessage == Network::room_result_message(Network::RoomResult::NotFound));
    Network::request_room(c.network, Network::RoomOperation::Create, roomName);
    wait([&] { return !c.network.pendingRoomRequest; });
    assert(c.network.roomMessage == Network::room_result_message(Network::RoomResult::NameTaken));
    Network::request_room(c.network, Network::RoomOperation::Create);
    wait([&] { return !c.network.room.name.empty(); });
    const auto publicRoom = c.network.room.name;
    assert(publicRoom.size()==4 && publicRoom.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")==std::string::npos);
    Network::request_room(b.network, Network::RoomOperation::List);
    wait([&] { return !b.network.pendingRoomRequest; });
    const auto listed = std::find_if(b.network.publicRooms.begin(),b.network.publicRooms.end(),[&](const auto& room) {return room.name == publicRoom;});
    assert(listed != b.network.publicRooms.end() && listed->players==1);
    Network::request_room(c.network, Network::RoomOperation::Leave);
    wait([&] { return c.network.room.name.empty(); });
    Network::request_room(c.network, Network::RoomOperation::Join, roomName);
    wait([&] { return !c.network.pendingRoomRequest; });
    assert(c.network.room.name.empty() && c.network.roomMessage.find("full") != std::string::npos);
    sendSignal(Network::ClientLobbySignal::ReadyForConfig);
    waitSignal(Network::ServerLobbySignal::ConfigExchangeReady);
    Network::LobbyPlayerSetup setup;
    setup.width = 10; setup.visibleHeight = 20; setup.turnPieces = 1; setup.hybridBoardSync=true;
    for (auto* client : {&a, &b}) Network::send(client->network, Network::encode_lobby_player_setup(setup));
    wait([&] {
        for (auto* client : {&a, &b}) {
            bool gotSetup = false;
            for (const auto& wire : client->network.onlineInbox)
                if (const auto peer=Network::decode_lobby_player_setup(wire)) {assert(peer->hybridBoardSync); gotSetup=true;}
            if (!gotSetup) return false;
        }
        return true;
    });
    sendSignal(Network::ClientLobbySignal::SetupReceived);
    waitSignal(Network::ServerLobbySignal::CountdownReady);
    sendSignal(Network::ClientLobbySignal::CountdownComplete);
    waitSignal(Network::ServerLobbySignal::RoundReady);
    Network::configure_board_sync(a.context,true,77); Network::configure_board_sync(b.context,true,77);
    Network::WebSocketSocket probe(url);
    probe.send("!probe");
    assert(probe.receive_for(5s) == "!probe-ok");
    std::string large(60000, 'x'); large[100] = '\0';
    Network::send(a.network, large);
    Network::send(b.network, "websocket-gameplay");
    wait([&] { return contains(a, "websocket-gameplay") && contains(b, large); });
    for (int variant = 0; variant < 3; ++variant) {
        for (auto* client : {&a,&b,&c}) client->network.onlineInbox.clear();
        const auto pose = Test::prepare_online_board(a.context,variant);
        Network::send_board_state_frame(a.context,a.session.player,pose);
        std::optional<Network::FramePacket> boardB;
        wait([&] { boardB = Network::try_receive_frame(b.context,10,20); return boardB.has_value(); });
        Test::check_online_board(*boardB,variant);
        assert(!Network::try_receive_frame(c.context,10,20));
        Network::send_board_state_frame(b.context,b.session.player,Test::prepare_online_board(b.context,variant));
        std::optional<Network::FramePacket> boardA;
        wait([&] { boardA = Network::try_receive_frame(a.context,10,20); return boardA.has_value(); });
        Test::check_online_board(*boardA,variant);
        const auto snapshots=a.network.boardSync.snapshotsSent;
        auto moved=pose.pose.translated(0,1);
        Network::send_piece_motion_frame(a.context,a.session.player,moved);
        std::optional<Network::FramePacket> motion;
        wait([&] { motion=Network::try_receive_frame(b.context,10,20); return motion.has_value(); });
        assert(motion->type==Network::FramePacketType::OpponentMotion && motion->motion && motion->motion->result==moved);
        assert(a.network.boardSync.snapshotsSent==snapshots && a.network.boardSync.motionsSent>0);
        const auto reverseSnapshots=b.network.boardSync.snapshotsSent;
        const auto reverse=pose.pose.translated(0,-1);
        Network::send_piece_motion_frame(b.context,b.session.player,reverse);
        std::optional<Network::FramePacket> reverseMotion;
        wait([&] { reverseMotion=Network::try_receive_frame(a.context,10,20); return reverseMotion.has_value(); });
        assert(reverseMotion->type==Network::FramePacketType::OpponentMotion &&
               reverseMotion->motion && reverseMotion->motion->result==reverse);
        assert(b.network.boardSync.snapshotsSent==reverseSnapshots && b.network.boardSync.motionsSent>0);
    }
    Network::send_board_state_frame(a.context,a.session.player,Network::ActivePieceState::locked(),Test::online_placement());
    std::optional<Network::FramePacket> placement;
    wait([&] { placement = Network::try_receive_frame(b.context,10,20); return placement.has_value(); });
    Test::check_online_placement(*placement);
    for (auto* client : {&a,&b}) client->context.config.turnPieces = 1;
    Network::configure_piece_turns(a.context,1,0,77); Network::configure_piece_turns(b.context,1,1,77);
    a.network.pieceTurns.completed = 2; b.network.pieceTurns.completed = 3;
    Network::resend_piece_turn(a.context,true); Network::resend_piece_turn(b.context,true);
    wait([&] {
        Network::try_receive_frame(a.context,10,20); Network::try_receive_frame(b.context,10,20);
        return a.network.pieceTurns.peerCompleted == 3 && b.network.pieceTurns.peerCompleted == 2;
    });
    Network::disconnect_online(a.network);
    wait([&] { return contains(b, "end2") && b.network.roomMembers.size()==1 &&
                    b.network.roomMembers[0].host; });
    assert(b.network.roomMembers.size()==1 && b.network.roomMembers[0].host);
    Network::request_room(c.network, Network::RoomOperation::Join, roomName);
    wait([&] { return c.network.room.name == roomName; });
    b.join(url); // Reconnect returns to room selection.
    wait([&] { return b.network.connection == ConnectionState::Connected; });
    assert(b.network.room.name.empty());
    Network::request_room(b.network, Network::RoomOperation::Join, roomName);
    wait([&] { return b.network.room.name == roomName; });
    Network::disconnect_online(c.network);
    // Force a failed transport, then reconnect to the same address: it must
    // create a new socket rather than reusing the stopped worker.
    b.network.webSocket->close();
    Network::poll_connection(b.network);
    assert(b.network.connection == ConnectionState::Failed);
    b.join(url);
    wait([&] { return b.network.connection == ConnectionState::Connected; });
    std::cerr << "network_websocket_test: OK\n";
}
