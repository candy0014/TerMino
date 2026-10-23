#include "tetr/compat/legacy_encoding.h"
#include "tetr/network/lobby_protocol.h"

#include <cassert>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace TetrTests::multiplayer_lobby_protocol_test {
namespace {

void lobby_signals_are_strictly_typed(){
    using Network::ClientLobbySignal;
    using Network::ServerLobbySignal;
    assert(Network::encode_server_lobby_signal(ServerLobbySignal::PairReady)=="start1");
    assert(Network::encode_server_lobby_signal(ServerLobbySignal::ConfigExchangeReady)=="start3");
    assert(Network::encode_server_lobby_signal(ServerLobbySignal::CountdownReady)=="start5");
    assert(Network::decode_server_lobby_signal("start1")==ServerLobbySignal::PairReady);
    assert(!Network::decode_server_lobby_signal("start2"));

    assert(Network::encode_client_lobby_signal(ClientLobbySignal::ReadyForConfig)=="start2");
    assert(Network::encode_client_lobby_signal(ClientLobbySignal::SetupReceived)=="start4");
    assert(Network::encode_client_lobby_signal(ClientLobbySignal::CountdownComplete)=="start6");
    assert(Network::encode_client_lobby_signal(ClientLobbySignal::Cancel)=="end_");
    assert(Network::decode_client_lobby_signal("start6")==ClientLobbySignal::CountdownComplete);
    assert(!Network::decode_client_lobby_signal("start5"));
}

void player_setup_round_trip_preserves_legacy_wire(){
    Network::LobbyPlayerSetup setup;
    setup.visibleHeight=20;
    setup.width=10;
    setup.nextCount=3;
    setup.holdEnabled=true;
    setup.nextPreview={Compat::piece_type_from_legacy(1),Compat::piece_type_from_legacy(12),Compat::piece_type_from_legacy(103)};
    setup.wins=7;
    setup.blockMaxHeight=4;
    setup.blockMaxWidth=6;

    const std::string wire=Network::encode_lobby_player_setup(setup);
    assert(wire=="20;10;3;1;1,12,103,;7;4;6;");
    auto hybridSetup=setup; hybridSetup.hybridBoardSync=true;
    const auto hybrid=Network::decode_lobby_player_setup(Network::encode_lobby_player_setup(hybridSetup));
    assert(hybrid && hybrid->hybridBoardSync && hybrid->nextPreview==setup.nextPreview);
    const auto decoded=Network::decode_lobby_player_setup(wire);
    assert(decoded);
    assert(decoded->visibleHeight==setup.visibleHeight);
    assert(decoded->width==setup.width);
    assert(decoded->nextCount==setup.nextCount);
    assert(decoded->holdEnabled==setup.holdEnabled);
    assert(decoded->nextPreview==setup.nextPreview);
    assert(decoded->wins==setup.wins);
    assert(decoded->blockMaxHeight==setup.blockMaxHeight);
    assert(decoded->blockMaxWidth==setup.blockMaxWidth);
}

void malformed_setup_is_rejected(){
    const std::vector<std::string> malformed{
        "", "20;10;3;1;1,2,3,;7;4;6", // missing final separator
        "20;10;3;2;1,2,3,;7;4;6;",     // invalid bool
        "20;10;2;1;1,2,3,;7;4;6;",     // preview count mismatch
        "20;10;3;1;1,x,3,;7;4;6;",     // invalid piece
        "20;-10;3;1;1,2,3,;7;4;6;",    // negative dimension
        "106;10;3;1;1,2,3,;7;4;6;",    // field too tall
        "20;106;3;1;1,2,3,;7;4;6;",    // field too wide
        "20;10;101;1;1,2,3,;7;4;6;",   // preview count too large
        "20;10;3;1;999999,2,3,;7;4;6;",// unknown piece id
        "20;10;3;1;1,2,3;7;4;6;",      // missing preview comma
        "20;10;3;1;1,2,3,;7;4;6;;",    // extra section
        "20;10;3;1;1,2,3,;7;2147483647;6;", // unsafe preview height
        "20;10;3;1;1,2,3,;7;4;2147483647;", // unsafe preview width
        "20;10;3;1;1,2,3,;7;106;6;",
        "20;10;3;1;1,2,3,;7;4;106;"
    };
    for(const auto& wire:malformed) assert(!Network::decode_lobby_player_setup(wire));
}

void randomized_setup_round_trip(){
    std::mt19937 rng(0x30C0DEC0u);
    for(int iteration=0;iteration<10000;++iteration){
        Network::LobbyPlayerSetup setup;
        setup.visibleHeight=static_cast<int>(rng()%101);
        setup.width=4+static_cast<int>(rng()%97);
        setup.nextCount=static_cast<int>(rng()%11);
        setup.holdEnabled=(rng()&1u)!=0;
        for(int i=0;i<setup.nextCount;++i) setup.nextPreview.push_back(Compat::piece_type_from_legacy(static_cast<int>(rng()%173)));
        setup.wins=static_cast<int>(rng()%1000);
        setup.blockMaxHeight=1+static_cast<int>(rng()%20);
        setup.blockMaxWidth=1+static_cast<int>(rng()%20);
        const auto decoded=Network::decode_lobby_player_setup(Network::encode_lobby_player_setup(setup));
        assert(decoded);
        assert(decoded->visibleHeight==setup.visibleHeight);
        assert(decoded->width==setup.width);
        assert(decoded->nextCount==setup.nextCount);
        assert(decoded->holdEnabled==setup.holdEnabled);
        assert(decoded->nextPreview==setup.nextPreview);
        assert(decoded->wins==setup.wins);
        assert(decoded->blockMaxHeight==setup.blockMaxHeight);
        assert(decoded->blockMaxWidth==setup.blockMaxWidth);
    }
}

void seed_codec_is_strict(){
    constexpr std::uint32_t seed=4294967295u;
    assert(Network::encode_lobby_seed(seed)=="4294967295");
    assert(Network::decode_lobby_seed("4294967295")==seed);
    assert(!Network::decode_lobby_seed("-1"));
    assert(!Network::decode_lobby_seed("42x"));
    assert(!Network::decode_lobby_seed("4294967296"));
}

void room_codec_validation() {
    using namespace Network;
    RoomRequest request{"abc123",42,RoomOperation::Create,"房间; name",false};
    const auto decoded = decode_room_request(encode_room_request(request));
    assert(decoded && decoded->name == request.name && !decoded->isPublic && decoded->id == 42);
    RoomResponse response{"abc123",42,RoomResult::Joined,{"房间; name",false,2},1,{},17,{{0,"Alice",true},{1,"测试玩家",false}}};
    const auto reply = decode_room_response(encode_room_response(response));
    assert(reply && reply->room.name == response.room.name && reply->room.players == 2 && reply->playerIndex == 1 && reply->stateRevision == 17);
    response.result = RoomResult::Listed; response.members.clear(); response.rooms = {{"ABCD",true,1},{"中文",true,2}};
    const auto list = decode_room_response(encode_room_response(response));
    assert(list && list->rooms.size() == 2 && list->rooms[1].name == "中文");
    for (const auto* wire : {"!room;a;0;0;;1","!room;a;1;4;;1","!room;a;1;0;GG;1","!room;a;1;0;;2","!room;a;1;0;A;1"}) assert(!decode_room_request(wire));
    for (const auto* wire : {"!room-reply;a;1;99;;1;0;-1;0", "!room-reply;a;1;1;41;1;3;0;0", "!room-reply;a;1;1;;1;1;0;0", "!room-reply;a;1;2;;1;0;-1;0;41;3"}) assert(!decode_room_response(wire));
    RoomResponse expanded{"abc123",0,RoomResult::Joined,{"Big room",true,4,8},3,{},20,{{0,"A",false},{1,"B",true},{2,"C",false},{3,"D",false}}};
    const auto four = decode_room_response(encode_room_response(expanded));
    assert(four && four->members.size() == 4 && four->room.capacity == 8);
    assert(!decode_room_response("!room-reply;a;0;1;41;1;2;0;1;32;0;32"));
    expanded.multiplayer=true;expanded.battleLimit=6;expanded.matchPhase=RoomMatchPhase::Preparing;expanded.matchEpoch=11;
    expanded.members[2].role=PlayerRole::Spectator;expanded.members[3].ready=true;
    const auto modern=decode_room_response(encode_room_response(expanded));
    assert(modern && modern->multiplayer && modern->battleLimit==6 && modern->members[2].role==PlayerRole::Spectator && modern->members[3].ready);
    expanded.multiplayer=false;
    expanded.hasPolicy=true; expanded.policy={false};
    const auto policy=decode_room_response(encode_room_response(expanded));
    assert(policy && policy->hasPolicy && !policy->policy.forceRules);
    RoomRequest transfer{"abc123",43,RoomOperation::TransferHost,"",true,3};
    const auto target=decode_room_request(encode_room_request(transfer));
    assert(target && target->operation==RoomOperation::TransferHost && target->targetSlot==3);
    transfer.operation=RoomOperation::SetPolicy; transfer.policy={true};
    const auto changed=decode_room_request(encode_room_request(transfer));
    assert(changed && changed->policy.forceRules);
    transfer.operation=RoomOperation::SetMemberRole;transfer.name="玩家; A";
    for(auto role:{PlayerRole::Battle,PlayerRole::Spectator,PlayerRole::Offline}) {
        transfer.memberRole=role;
        const auto managed=decode_room_request(encode_room_request(transfer));
        assert(managed && managed->operation==RoomOperation::SetMemberRole && managed->targetSlot==3 &&
            managed->name==transfer.name && managed->memberRole==role);
    }
    for(const auto* wire:{"!room;abc;1;13;;1;round-v1;3;3;0","!room;abc;1;13;;1;round-v1;3;-1;0","!room;abc;1;13;;1;round-v1;3;2x;0"})
        assert(!decode_room_request(wire));
    const auto username = decode_username_update(encode_username_update({"abc123","玩家; A",12}));
    assert(username && username->username == "玩家; A" && username->revision == 12);
    assert(!decode_username_update("!username;abc;1B"));
    assert(valid_room_name("中文; name"));
    assert(valid_room_name("",true) && !valid_room_name(""));
    assert(!valid_room_name(" A") && !valid_room_name("A\n") && !valid_room_name(std::string(49,'x')));
    assert(!valid_room_name("\xc0\x80") && !valid_room_name("\xed\xa0\x80"));
    assert(normalize_room_name(" \tABCD \n") == "ABCD");
}

} // namespace

int run(int argc,char** argv){
    lobby_signals_are_strictly_typed();
    player_setup_round_trip_preserves_legacy_wire();
    malformed_setup_is_rejected();
    randomized_setup_round_trip();
    seed_codec_is_strict();
    room_codec_validation();
    return 0;
}
} // namespace TetrTests::multiplayer_lobby_protocol_test
