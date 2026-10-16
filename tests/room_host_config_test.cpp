#include "tetr/config/multiplayer_config.h"
#include "tetr/config/normalization.h"
#include "tetr/network/network_services.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/transport.h"
#include "tetr/input/services.h"
#include "tetr/replay/session_state.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/piece.h"
#include "../src/app/init_multiplayer.h"
#include "../src/app/init_replay.h"
#include <algorithm>
#include <cassert>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
struct Client {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};
    UserConfig::Settings user;
    Input::Services input;
    Replay::ReplaySessionState replay;
    Network::NetworkServices network;
    Client() {user.gameMode=4; context.attach_user_settings(user); context.attach_input(input); context.attach_replay(replay); context.attach_network(network);}
    ~Client() {Network::disconnect_online(network);}
};
int main(int argc,char** argv) {
    using namespace Network; using namespace std::chrono_literals;
    const bool publicTest=argc>1;
    std::unique_ptr<RelayServer> server;
    if (!publicTest) {server=std::make_unique<RelayServer>(718); server->bind(3004);}
    const std::string address=publicTest?argv[1]:"127.0.0.1:3004";
    Client a,b;
    bool deferGuest=false;
    const auto contains=[](const Client& c,const std::string& wire) {
        return std::find(c.network.onlineInbox.begin(),c.network.onlineInbox.end(),wire)!=c.network.onlineInbox.end();
    };
    auto pump=[&] {
        if (server) {
            server->poll(1ms);
            // Uploads arrive in 32-part bursts. Drain them as the real relay
            // does, rather than building a backlog behind the fixture's sleep.
            for(int i=0;i<63;++i)server->poll(0ms);
        }
        for (auto* client:{&a,&b}) {
            poll_connection(client->network);
            auto& state=client->network.roomConfig;
            if (state.incoming.payload && !state.applied && !(deferGuest && client==&a)) {
                const auto config=UserConfig::decode_multiplayer_config(*state.incoming.payload); assert(config);
                client->context.config=UserConfig::apply_multiplayer_config(client->user,client->context.config,*config,state.policy.forceRules);
                accept_room_config(client->network);
            }
        }
        std::this_thread::sleep_for(1ms);
    };
    auto wait=[&](const auto& predicate) {
        const auto deadline=std::chrono::steady_clock::now()+15s;
        do {pump(); if (predicate()) return;} while (std::chrono::steady_clock::now()<deadline);
        std::cerr<<a.network.connectionMessage<<" / "<<a.network.roomMessage<<'\n'<<b.network.connectionMessage<<" / "<<b.network.roomMessage<<'\n';
        assert(false && "host configuration integration timed out");
    };
    const auto request=[&](Client& c,RoomOperation op,const std::string& name="") {
        request_room(c.network,op,name,false); wait([&] {return !c.network.pendingRoomRequest;});
    };
    initialize_network_services(a.context,address,3); initialize_network_services(b.context,address,3);
    wait([&] {return a.network.connection==ConnectionState::Connected && b.network.connection==ConnectionState::Connected;});
    const auto room="HostConfig-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    request(a,RoomOperation::Create,room); request(b,RoomOperation::Join,room);
    wait([&] {return a.network.roomMembers.size()==2 && b.network.roomMembers.size()==2 && a.network.roomPolicyAvailable && b.network.roomPolicyAvailable;});
    assert(a.network.roomPolicy.forceRules && is_room_host(a.network));
    transfer_room_host(b.network,0); wait([&] {return !b.network.pendingRoomRequest;});
    assert(b.network.roomMessage==room_result_message(RoomResult::HostOnly) && is_room_host(a.network));
    transfer_room_host(a.network,7); wait([&] {return !a.network.pendingRoomRequest;});
    assert(a.network.roomMessage==room_result_message(RoomResult::InvalidPlayer));
    transfer_room_host(a.network,1);
    wait([&] {return !a.network.pendingRoomRequest && is_room_host(b.network) && !is_room_host(a.network);});
    const auto retryId=a.network.nextRoomRequest;
    RoomRequest retry{a.network.connectionToken,retryId,RoomOperation::TransferHost,"",true,1};
    send_online(a.network,encode_room_request(retry)); pump(); // Exactly-once UDP command retry.
    set_room_policy(a.network,{false}); wait([&] {return !a.network.pendingRoomRequest;});
    assert(a.network.roomMessage==room_result_message(RoomResult::HostOnly) && a.network.roomPolicy.forceRules);
    a.user.fieldWidth=10; a.user.nextCount=5; a.user.das=.42; a.user.arr=.03; a.user.squareStyle=0;
    b.user.forceDas=1;b.user.forcedDas=.025; b.user.fieldWidth=14; b.user.nextCount=2; b.user.das=.07; b.user.arr=0; b.user.squareStyle=1; b.user.hitMultiplier=3.5;
    Runtime::MapDefinition map; map.name="Host-only map"; map.id=891;
    map.generator=Runtime::MapGenerator::CustomSequence; map.rows.back()[0]='G';
    const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    for (int i=0;i<5000;++i) map.sequence.push_back(types[i%types.size()]);
    b.user.mapId=map.id;
    const auto prepare=[&] {
        for (auto* c:{&a,&b}) {
            c->context.config=Runtime::normalize_user_config(c->user,Runtime::GameMode::Multiplayer);
            if (c==&b) Runtime::apply_map_config(c->context.config,map);
            c->network.roomConfig={}; c->network.onlineInbox.clear();
            c->network.localRoomConfig=UserConfig::encode_multiplayer_config(c->user,c->context.config);
        }
    };
    prepare();
    const auto preview=[&] {return UserConfig::encode_multiplayer_config(b.user,b.context.config,true);};
    set_room_config_preview(b.network,preview());
    wait([&] {return a.network.roomPreview.incoming.payload.has_value();});
    const auto snapshot=UserConfig::decode_multiplayer_config(*a.network.roomPreview.incoming.payload);
    assert(snapshot && snapshot->map && *snapshot->map==map && snapshot->settings.forceDas);
    assert(!a.network.roomConfig.epoch && !contains(a,"start3")); // Browsing cannot start or apply match rules.
    const auto previousPreview=a.network.roomPreview.incoming.digest;
    b.user.forcedDas=.031;
    set_room_config_preview(b.network,preview());
    wait([&] {return a.network.roomPreview.incoming.payload && a.network.roomPreview.incoming.digest!=previousPreview;});
    assert(UserConfig::decode_multiplayer_config(*a.network.roomPreview.incoming.payload)->settings.forcedDas==.031);
    assert(a.user.das==.42);
    const auto firstHostEpoch=a.network.roomPreview.epoch;
    transfer_room_host(b.network,0);wait([&]{return !b.network.pendingRoomRequest && is_room_host(a.network);});
    assert(!b.network.roomPreview.incoming.payload);
    set_room_config_preview(a.network,UserConfig::encode_multiplayer_config(a.user,a.context.config,true));
    wait([&]{return b.network.roomPreview.incoming.payload.has_value();});
    assert(b.network.roomPreview.epoch>firstHostEpoch);
    const auto secondHostEpoch=b.network.roomPreview.epoch;
    transfer_room_host(a.network,1);wait([&]{return !a.network.pendingRoomRequest && is_room_host(b.network);});
    assert(!a.network.roomPreview.incoming.payload);
    set_room_config_preview(b.network,preview());
    wait([&]{return a.network.roomPreview.incoming.payload.has_value();});
    assert(a.network.roomPreview.epoch>secondHostEpoch);
    for (const RoomPolicy policy: {RoomPolicy{true},RoomPolicy{false}}) {
        set_room_policy(b.network,policy);
        wait([&] {return !b.network.pendingRoomRequest && a.network.roomPolicy==policy && b.network.roomPolicy==policy;});
        prepare(); deferGuest=policy.forceRules;
        for (auto* c:{&a,&b}) send(c->network,"start2");
        if (deferGuest) {
            wait([&] {return a.network.roomConfig.incoming.payload.has_value();});
            assert(!contains(a,"start3") && !contains(b,"start3")); // No start before all clients APPLY the full configuration.
            set_room_policy(b.network,{false}); wait([&] {return !b.network.pendingRoomRequest;});
            assert(b.network.roomMessage==room_result_message(RoomResult::RoundBusy));
            deferGuest=false;
        }
        wait([&] {return contains(a,"start3") && contains(b,"start3");});
        assert(a.context.config.handling.das.seconds()==(policy.forceRules?b.user.forcedDas:a.user.das));
        assert(a.context.config.field.nextCount==(policy.forceRules?b.user.nextCount:a.user.nextCount));
        assert(a.context.config.garbage.hitMultiplier==(policy.forceRules?b.user.hitMultiplier:a.user.hitMultiplier));
        assert(a.context.config.display.squareStyle==a.user.squareStyle && b.context.config.display.squareStyle==b.user.squareStyle);
        if (policy.forceRules) assert(a.context.config.initialMap && *a.context.config.initialMap==map);
        else assert(!a.context.config.initialMap);
        assert(a.user.fieldWidth==10 && a.user.das==.42 && !a.user.mapId); // Match rules don't overwrite the profile.
        send(a.network,"end_"); wait([&] {return contains(b,"end2");});
        for (auto* c:{&a,&b}) c->network.onlineInbox.clear();
    }
    // Exercise the actual application pre-start path, including replay prefix
    // replacement, before either client can initialize its board or countdown.
    set_room_policy(b.network,{true}); wait([&] {return !b.network.pendingRoomRequest && a.network.roomPolicy==RoomPolicy{true};});
    // A legacy peer cannot silently bypass either enforcement switch.
    send_online(a.network,"!join;"+a.network.connectionToken);
    for (auto* c:{&a,&b}) send(c->network,"start2");
    wait([&] {return a.network.roomMessage.find("Update all players")!=std::string::npos && b.network.roomMessage.find("Update all players")!=std::string::npos;});
    send_online(a.network,"!join-v3;"+a.network.connectionToken);
    wait([&] {return a.network.roomPolicyAvailable && a.network.roomPolicy==RoomPolicy{true};});
    prepare();
    a.user.mapId=2147483646; // Unavailable local selection is superseded by the incoming host map.
    auto mode=Runtime::GameMode::Multiplayer;
    assert(!App::InitDetail::configure_replay_or_local_game(a.context,mode,nullptr));
    Render::begin_output_suppression();
    auto first=std::async(std::launch::async,[&] {return App::InitDetail::wait_for_multiplayer_lobby(a.context);});
    auto second=std::async(std::launch::async,[&] {return App::InitDetail::wait_for_multiplayer_lobby(b.context);});
    const auto deadline=std::chrono::steady_clock::now()+15s;
    while (first.wait_for(0ms)!=std::future_status::ready || second.wait_for(0ms)!=std::future_status::ready) {
        if (server) server->poll(1ms); else std::this_thread::sleep_for(1ms);
        assert(std::chrono::steady_clock::now()<deadline);
    }
    assert(first.get()==App::InitDetail::LobbyWaitResult::Ready && second.get()==App::InitDetail::LobbyWaitResult::Ready);
    Render::end_output_suppression();
    assert(a.context.config.initialMap && *a.context.config.initialMap==map && a.context.config.handling.das==b.context.config.handling.das);
    assert(a.user.mapId==2147483646); // No host map ID is written into the guest's collection/profile.
    assert(a.replay.configPrefix.find("{Map,")!=std::string::npos);
    send(a.network,"end_"); wait([&] {return contains(b,"end2");});
    disconnect_online(b.network);
    wait([&] {return a.network.roomMembers.size()==1 && is_room_host(a.network);});
    assert(a.network.roomPolicy==RoomPolicy({true}));
    std::cerr<<"room_host_config_test: OK ("<<(publicTest?"public WSS":"UDP")<<")\n";
}
