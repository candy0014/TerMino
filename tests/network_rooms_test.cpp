#include "tetr/network/relay_server.h"
#include "tetr/network/network_services.h"
#include "tetr/network/transport.h"
#include "tetr/runtime/game_context.h"
#include "support/network_gameplay_frames.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>

namespace TetrTests::network_rooms_test {
struct Client {
    // GameContext carries large session buffers: do not allocate six on the stack.
    std::unique_ptr<Runtime::SessionState> session = std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session, services};
    Network::NetworkServices network;
    Client() { context.attach_network(network); }
};
int run(int argc,char** argv) {
    using namespace Network;
    using namespace std::chrono_literals;
    RelayServer server(341);
    server.bind(3002);
    Client a, b, c, d, e, f;
    auto pump = [&] {
        for (int i = 0; i < 35; ++i) {
            server.poll(1ms);
            for (auto* client : {&a,&b,&c,&d,&e,&f}) poll_connection(client->network);
        }
    };
    const auto request = [&](Client& client, RoomOperation op, const std::string& name = "", bool isPublic = true) {
        request_room(client.network, op, name, isPublic); pump();
        assert(!client.network.pendingRoomRequest);
    };
    for (auto* client : {&a,&b,&c,&d,&e,&f}) initialize_network_services(client->context,"127.0.0.1:3002",3);
    pump();
    for (auto* client : {&a,&b,&c,&d,&e,&f}) {
        assert(client->network.connection == ConnectionState::Connected);
        assert(client->network.room.name.empty() && client->network.onlineInbox.empty());
        // Readiness without membership cannot pair connected clients.
        send(client->network,"start2");
    }
    pump();
    for (auto* client : {&a,&b,&c,&d,&e,&f}) assert(client->network.onlineInbox.empty());
    set_online_username(a.network,"Creator");
    set_online_username(b.network,"Guest 玩家"); pump();
    assert(a.network.confirmedUsername == "Creator" && b.network.confirmedUsername == "Guest 玩家");
    request(a,RoomOperation::Create,"  对战; A  ");
    assert(a.network.room.name == "对战; A" && a.network.room.players == 1);
    request(b,RoomOperation::Create,"对战; A",false);
    assert(b.network.room.name.empty() && b.network.roomMessage == room_result_message(RoomResult::NameTaken));
    request(b,RoomOperation::Join,"missing");
    assert(b.network.room.name.empty() && b.network.roomMessage == room_result_message(RoomResult::NotFound));
    request(b,RoomOperation::Join,"对战; A");
    assert(a.network.room.players == 2 && b.network.room.players == 2);
    assert(a.network.roomMembers.size() == 2 && a.network.roomMembers[0].username == "Creator" && a.network.roomMembers[0].host);
    set_online_username(a.network,"新房主名字"); pump();
    assert(a.network.confirmedUsername == "新房主名字" && b.network.roomMembers[0].username == "新房主名字" && b.network.roomMembers[0].host);
    send_online(a.network,encode_username_update({a.network.connectionToken,"Old late name",a.network.usernameRevision-1})); pump();
    assert(b.network.roomMembers[0].username == "新房主名字"); // Stale UDP rename cannot revert the latest name.
    set_online_username(a.network,"Temporary"); poll_connection(a.network);
    set_online_username(a.network,"新房主名字"); pump();
    assert(a.network.confirmedUsernameRevision == a.network.usernameRevision && b.network.roomMembers[0].username == "新房主名字");
    request(e,RoomOperation::Join,"对战; A");
    assert(e.network.room.name.empty() && e.network.roomMessage == room_result_message(RoomResult::Full));
    request(c,RoomOperation::Create,"Secret",false);
    request(d,RoomOperation::Join,"Secret");
    request(e,RoomOperation::List);
    assert(e.network.publicRooms.size() == 1 && e.network.publicRooms[0].name == "对战; A" && e.network.publicRooms[0].players == 2);
    request(e,RoomOperation::Create,"",true);
    const auto generated = e.network.room.name;
    assert(generated.size() == 4 && generated.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos);
    // Resend the exact same create request after the response: it must not
    // allocate a second room or change the auto-assigned name.
    const RoomRequest duplicate{e.network.connectionToken,e.network.nextRoomRequest,RoomOperation::Create,"",true};
    send_online(e.network,encode_room_request(duplicate)); pump();
    assert(e.network.room.name == generated);
    request(f,RoomOperation::List);
    assert(f.network.publicRooms.size() == 2);
    request(e,RoomOperation::Create,"other");
    assert(e.network.roomMessage == room_result_message(RoomResult::AlreadyInRoom));
    request(f,RoomOperation::Join,"");
    assert(f.network.roomMessage == room_result_message(RoomResult::InvalidName));
    request(f,RoomOperation::Create,std::string(49,'x'));
    assert(f.network.roomMessage == room_result_message(RoomResult::InvalidName));
    request(f,RoomOperation::Create,"bad\x1bname");
    assert(f.network.roomMessage == room_result_message(RoomResult::InvalidName));
    request(f,RoomOperation::Join,generated);
    assert(e.network.room.players == 2 && f.network.room.players == 2);
    // Start two independent rounds. A third pair remains in its room menu.
    set_room_policy(a.network,{false}); set_room_policy(c.network,{false}); pump();
    assert(a.network.roomPolicy==RoomPolicy({false}) && c.network.roomPolicy==RoomPolicy({false}));
    for (auto* client : {&a,&b,&c,&d}) send(client->network,"start2");
    pump();
    LobbyPlayerSetup setup; setup.width = 10; setup.visibleHeight = 20; setup.turnPieces = 1;
    for (auto* client : {&a,&b,&c,&d}) {
        auto announced=setup; announced.hybridBoardSync=client==&a || client==&c;
        send(client->network,encode_lobby_player_setup(announced));
    }
    pump();
    for (auto* client : {&a,&b,&c,&d}) {
        bool exchanged=false;
        for (const auto& wire:client->network.onlineInbox) if (const auto peer=decode_lobby_player_setup(wire)) {
            assert(!peer->hybridBoardSync); exchanged=true; // Older peers receive the unextended setup.
        }
        assert(exchanged);
    }
    for (const auto* signal : {"start4","start6"}) {
        for (auto* client : {&a,&b,&c,&d}) send(client->network,signal);
        pump();
    }
    for (auto* client : {&a,&b,&c,&d,&e,&f}) client->network.onlineInbox.clear();
    send(a.network,"room-A-only"); send(c.network,"room-Secret-only"); pump();
    const auto contains = [](const Client& client, const std::string& wire) {
        const auto& inbox = client.network.onlineInbox;
        return std::find(inbox.begin(),inbox.end(),wire) != inbox.end();
    };
    assert(contains(b,"room-A-only") && contains(d,"room-Secret-only"));
    for (const auto* client : {&a,&c,&d,&e,&f}) assert(!contains(*client,"room-A-only"));
    for (const auto* client : {&a,&b,&c,&e,&f}) assert(!contains(*client,"room-Secret-only"));
    // Real board frames and turn updates use ! just like room controls.
    // Both relay routing and client inbox filtering must admit these frames.
    for (int variant = 0; variant < 3; ++variant) {
        for (auto* client : {&a,&b,&c,&d,&e,&f}) client->network.onlineInbox.clear();
        const auto aPose = Test::prepare_online_board(a.context,variant);
        const auto cPose = Test::prepare_online_board(c.context,variant);
        send_board_state_frame(a.context,a.session->player,aPose);
        send_board_state_frame(c.context,c.session->player,cPose);
        pump();
        const auto boardB = try_receive_frame(b.context,10,20), boardD = try_receive_frame(d.context,10,20);
        assert(boardB && boardD);
        Test::check_online_board(*boardB,variant); Test::check_online_board(*boardD,variant);
        assert(!try_receive_frame(b.context,10,20) && !try_receive_frame(d.context,10,20));
        for (auto* client : {&a,&c,&e,&f}) assert(!try_receive_frame(client->context,10,20));
        // Reverse direction must update the creator's opponent as well.
        send_board_state_frame(b.context,b.session->player,Test::prepare_online_board(b.context,variant));
        pump();
        const auto boardA = try_receive_frame(a.context,10,20); assert(boardA);
        Test::check_online_board(*boardA,variant);
    }
    send_board_state_frame(a.context,a.session->player,ActivePieceState::locked(),Test::online_placement());
    pump();
    const auto placement = try_receive_frame(b.context,10,20); assert(placement); Test::check_online_placement(*placement);
    for (auto* client : {&a,&b}) client->context.config.turnPieces = 1;
    configure_piece_turns(a.context,1,0,77); configure_piece_turns(b.context,1,1,77);
    a.network.pieceTurns.completed = 2; b.network.pieceTurns.completed = 3;
    resend_piece_turn(a.context,true); resend_piece_turn(b.context,true); pump();
    assert(!try_receive_frame(a.context,10,20) && !try_receive_frame(b.context,10,20));
    assert(a.network.pieceTurns.peerCompleted == 3 && b.network.pieceTurns.peerCompleted == 2);
    send_online(a.network,"!unknown-control;should-not-relay"); pump();
    assert(!contains(b,"!unknown-control;should-not-relay"));
    // Exercise loss recovery on the real UDP relay, independently of the pure
    // timeline tests. Both endpoints opt in after the legacy interoperability checks.
    configure_board_sync(a.context,true,99); configure_board_sync(b.context,true,99);
    for (auto* client : {&a,&b,&c,&d,&e,&f}) client->network.onlineInbox.clear();
    auto pose=Test::prepare_online_board(a.context,0).pose;
    send_board_state_frame(a.context,a.session->player,ActivePieceState::active(pose)); pump();
    b.network.onlineInbox.clear(); // Simulate losing the initial full checkpoint.
    pose=pose.translated(0,1); send_piece_motion_frame(a.context,a.session->player,pose);
    std::optional<FramePacket> recovered;
    const auto recoveryDeadline=std::chrono::steady_clock::now()+2s;
    do {
        pump(); try_receive_frame(a.context,10,20); recovered=try_receive_frame(b.context,10,20);
        if (recovered) break;
    } while (std::chrono::steady_clock::now()<recoveryDeadline);
    assert(recovered && recovered->checkpoint && recovered->opponentState->activePose==pose);
    assert(a.network.boardSync.snapshotsSent>=2 && !b.network.boardSync.incoming.needs_snapshot());
    // Requests may be retried while the first response traverses the jitter
    // buffer. Drain these legitimate duplicate recovery checkpoints first.
    const auto settledAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(150);
    while (std::chrono::steady_clock::now()<settledAt) {
        pump(); try_receive_frame(a.context,10,20);
        while (try_receive_frame(b.context,10,20)) {}
    }
    const auto snapshots=a.network.boardSync.snapshotsSent;
    const auto staleState=encode_board_state(a.context,a.session->player,ActivePieceState::active({{2,3},Runtime::Rotation{}}));
    const auto stale=encode_board_checkpoint({99,1,0,1,board_state_fingerprint(staleState)},staleState);
    pose=pose.translated(0,1); send_piece_motion_frame(a.context,a.session->player,pose);
    pose=pose.translated(0,1); send_piece_motion_frame(a.context,a.session->player,pose); pump();
    const auto lost=std::find_if(b.network.onlineInbox.begin(),b.network.onlineInbox.end(),[](const auto& wire) {
        return wire.rfind("!motion-v1;",0)==0;
    });
    assert(lost!=b.network.onlineInbox.end()); b.network.onlineInbox.erase(lost);
    std::optional<FramePacket> surviving;
    const auto motionDeadline=std::chrono::steady_clock::now()+1s;
    do {pump(); surviving=try_receive_frame(b.context,10,20);} while (!surviving && std::chrono::steady_clock::now()<motionDeadline);
    assert(surviving && surviving->motion && surviving->motion->result==pose);
    assert(a.network.boardSync.snapshotsSent==snapshots);
    send(a.network,stale); pump(); assert(!try_receive_frame(b.context,10,20));
    assert(!try_receive_frame(c.context,10,20) && !try_receive_frame(d.context,10,20));
    // Final keyframe is presented before the terminal signal, without making
    // attack/turn traffic wait for the normal presentation buffer.
    send_board_state_frame(a.context,a.session->player,ActivePieceState::active(pose),std::nullopt,true);
    send_round_signal(a.context,OutgoingRoundSignal::LocalLoss); pump();
    const auto finalBoard=try_receive_frame(b.context,10,20); assert(finalBoard && finalBoard->checkpoint);
    const auto won=try_receive_frame(b.context,10,20); assert(won && won->type==FramePacketType::Win);
    // Leaving interrupts only the same room, updates its count and keeps the
    // server connection available for subsequent create/join requests.
    request(a,RoomOperation::Leave);
    assert(a.network.room.name.empty() && a.network.connection == ConnectionState::Connected);
    assert(b.network.room.players == 1 && contains(b,"end2"));
    assert(b.network.roomMembers.size() == 1 && b.network.roomMembers[0].username == "Guest 玩家" && b.network.roomMembers[0].host);
    send(c.network,"other-room-keeps-playing"); pump(); assert(contains(d,"other-room-keeps-playing"));
    request(b,RoomOperation::Leave);
    request(a,RoomOperation::Create,"对战; A"); // Last departure released the name.
    assert(a.network.room.name == "对战; A");
    request(b,RoomOperation::List);
    const auto found = std::find_if(b.network.publicRooms.begin(),b.network.publicRooms.end(),[](const auto& room) {return room.name == "对战; A";});
    assert(found != b.network.publicRooms.end() && found->players == 1);
    for (auto* client : {&a,&b,&c,&d,&e,&f}) disconnect_online(client->network);
    pump();
    std::cerr << "network_rooms_test: OK\n";
    return 0;
}
} // namespace TetrTests::network_rooms_test
