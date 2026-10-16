#include "tetr/network/relay_server.h"
#include "tetr/network/network_services.h"
#include "tetr/network/transport.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/runtime/game_context.h"
#include "tetr/app/application_state.h"
#include "tetr/config/user_config.h"
#include "tetr/input/services.h"
#include "../src/app/multiplayer_battle.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

struct Member {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    UserConfig::Settings user;Network::NetworkServices network;Input::Services input;
    Member(){context.attach_network(network);context.attach_user_settings(user);context.attach_input(input);}
    ~Member(){Network::disconnect_online(network);}
};
int main(int argc,char** argv) {
    using namespace Network;using namespace std::chrono_literals;
    std::unique_ptr<RelayServer> server;
    if(argc<2){server=std::make_unique<RelayServer>(1703);server->bind(3007);}
    Member a,b,c,d,e;Member* peers[]{&a,&b,&c,&d,&e};
    const auto endpoint=argc>1?argv[1]:"127.0.0.1:3007";
    const auto pump=[&] {
        if(server)server->poll(1ms);
        for(auto* peer:peers)poll_connection(peer->network);
        std::this_thread::sleep_for(1ms);
    };
    const auto wait=[&](const auto& done) {
        const auto deadline=std::chrono::steady_clock::now()+10s;
        do{pump();if(done())return;}while(std::chrono::steady_clock::now()<deadline);
        assert(false && "kick regression timed out");
    };
    const auto request=[&](Member& peer,RoomOperation operation,int slot=-1,const std::string& name="") {
        request_room(peer.network,operation,name,false,false,slot);
        wait([&]{return !peer.network.pendingRoomRequest;});
    };
    const auto manage=[&](Member& peer,Member& target,PlayerRole role,const std::string& selectedName="") {
        request_room(peer.network,RoomOperation::SetMemberRole,selectedName.empty()?target.user.username:selectedName,
            false,false,target.network.roomPlayerIndex,{},kMultiplayerRoomCapacity,role);
        wait([&]{return !peer.network.pendingRoomRequest;});
    };
    const auto roleOf=[](const Member& peer,int slot) {
        const auto found=std::find_if(peer.network.roomMembers.begin(),peer.network.roomMembers.end(),[&](const auto& m){return m.slot==slot;});
        assert(found!=peer.network.roomMembers.end());return found->role;
    };
    for(int i=0;i<5;++i){peers[i]->user.username="Kick-test-"+std::to_string(i);initialize_network_services(peers[i]->context,endpoint);}
    wait([&]{for(auto* p:peers)if(p->network.connection!=ConnectionState::Connected || p->network.confirmedUsername!=p->user.username)return false;return true;});
    const auto name="Kick-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    request(a,RoomOperation::Create,-1,name);
    for(auto* p:{&b,&c,&d})request(*p,RoomOperation::Join,-1,name);
    wait([&]{return a.network.room.players==4;});
    // Mode changes automatically leave the online roster, including for the
    // host. A queued unrelated request must not lose that transition.
    for(auto* p:{&a,&d}) {
        p->user.gameMode=4;synchronize_room_player_mode(p->network,true,p->user.gameMode);
        request_room(p->network,RoomOperation::List);
        p->user.gameMode=0;
        wait([&]{
            synchronize_room_player_mode(p->network,false,p->user.gameMode);
            return !p->network.pendingRoomRequest && roleOf(*p,p->network.roomPlayerIndex)==PlayerRole::Offline;
        });
        assert(p->network.room.name==name);
        for(auto role:{PlayerRole::Spectator,PlayerRole::Battle}) {
            request(*p,RoomOperation::SetRole,static_cast<int>(role));
            p->user.gameMode=4;synchronize_room_player_mode(p->network,true,p->user.gameMode);
            for(int tick=0;tick<5;++tick)pump();
            assert(roleOf(*p,p->network.roomPlayerIndex)==role);
            p->user.gameMode=2;synchronize_room_player_mode(p->network,false,p->user.gameMode);
            wait([&]{return roleOf(*p,p->network.roomPlayerIndex)==PlayerRole::Offline;});
        }
        request(*p,RoomOperation::SetRole,0);p->user.gameMode=4;
        synchronize_room_player_mode(p->network,true,p->user.gameMode);
    }
    manage(b,c,PlayerRole::Spectator);assert(b.network.roomMessage==room_result_message(RoomResult::HostOnly));
    manage(e,c,PlayerRole::Spectator);assert(e.network.roomMessage==room_result_message(RoomResult::NotFound));
    manage(a,c,PlayerRole::Spectator,"Stale target name");assert(a.network.roomMessage==room_result_message(RoomResult::InvalidPlayer));
    request(a,RoomOperation::SetMemberRole,31);assert(a.network.roomMessage==room_result_message(RoomResult::InvalidPlayer));
    manage(a,c,PlayerRole::Spectator);
    wait([&]{return roleOf(c,c.network.roomPlayerIndex)==PlayerRole::Spectator && roleOf(b,c.network.roomPlayerIndex)==PlayerRole::Spectator;});
    manage(a,c,PlayerRole::Battle);assert(roleOf(a,c.network.roomPlayerIndex)==PlayerRole::Battle);
    manage(a,d,PlayerRole::Offline);
    wait([&]{return roleOf(d,d.network.roomPlayerIndex)==PlayerRole::Offline;});
    d.user.gameMode=0;synchronize_room_player_mode(d.network,false,d.user.gameMode);
    manage(b,d,PlayerRole::Spectator);assert(b.network.roomMessage==room_result_message(RoomResult::HostOnly));
    for(auto role:{PlayerRole::Spectator,PlayerRole::Battle}) {
        manage(a,d,role);
        wait([&]{for(auto* p:{&a,&b,&c,&d})if(roleOf(*p,d.network.roomPlayerIndex)!=role)return false;return true;});
        for(int tick=0;tick<5;++tick) {synchronize_room_player_mode(d.network,false,d.user.gameMode);pump();}
        assert(roleOf(d,d.network.roomPlayerIndex)==role);
    }
    // Another non-online mode selection must trigger Offline again even
    // though both modes have the same online=false value.
    d.user.gameMode=1;synchronize_room_player_mode(d.network,false,d.user.gameMode);
    wait([&]{return roleOf(d,d.network.roomPlayerIndex)==PlayerRole::Offline;});
    request(d,RoomOperation::SetRole,1);d.user.gameMode=4;
    synchronize_room_player_mode(d.network,true,d.user.gameMode);
    request_room(a.network,RoomOperation::SetLimits,{},true,false,32,{},2);
    wait([&]{return !a.network.pendingRoomRequest;});
    assert(roleOf(a,c.network.roomPlayerIndex)==PlayerRole::Spectator);
    manage(a,c,PlayerRole::Battle);assert(a.network.roomMessage==room_result_message(RoomResult::BattleFull));
    manage(a,b,PlayerRole::Battle);assert(a.network.roomMessage==room_result_message(RoomResult::RoleChanged));
    manage(a,b,PlayerRole::Spectator);manage(a,c,PlayerRole::Battle);
    assert(roleOf(a,c.network.roomPlayerIndex)==PlayerRole::Battle);
    manage(a,b,PlayerRole::Battle);assert(a.network.roomMessage==room_result_message(RoomResult::BattleFull));
    request_room(a.network,RoomOperation::SetLimits,{},true,false,32,{},32);
    wait([&]{return !a.network.pendingRoomRequest;});
    manage(a,b,PlayerRole::Battle);manage(a,d,PlayerRole::Battle);
    request(b,RoomOperation::Kick,c.network.roomPlayerIndex,c.user.username);
    assert(b.network.roomMessage==room_result_message(RoomResult::HostOnly));
    request(e,RoomOperation::Kick,1);assert(e.network.roomMessage==room_result_message(RoomResult::NotFound));
    request(a,RoomOperation::Kick,a.network.roomPlayerIndex,a.user.username);
    assert(a.network.roomMessage==room_result_message(RoomResult::InvalidPlayer));
    request(a,RoomOperation::Kick,31);assert(a.network.roomMessage==room_result_message(RoomResult::InvalidPlayer));
    request(a,RoomOperation::Kick,c.network.roomPlayerIndex,"Stale target name");
    assert(a.network.roomMessage==room_result_message(RoomResult::InvalidPlayer));
    request(a,RoomOperation::SetRole,2);request(d,RoomOperation::SetRole,2);
    const auto dSlot=d.network.roomPlayerIndex;
    request(a,RoomOperation::Kick,dSlot,d.user.username);
    const RoomRequest duplicate{a.network.connectionToken,a.network.nextRoomRequest,RoomOperation::Kick,d.user.username,false,dSlot};
    wait([&]{return d.network.room.name.empty() && a.network.room.players==3;});
    assert(is_room_host(a.network) && a.network.roomMembers.front().role==PlayerRole::Offline);
    assert(d.network.connection==ConnectionState::Connected && d.network.roomMessage=="You were kicked from the room.");
    assert(d.network.matchCanceled && d.network.playerInbox.empty());
    request(d,RoomOperation::Join,-1,name);
    send_online(a.network,encode_room_request(duplicate));
    for(int i=0;i<30;++i)pump();
    assert(d.network.room.name==name && a.network.room.players==4);
    request(a,RoomOperation::SetRole,0);request(d,RoomOperation::SetRole,0);
    request(a,RoomOperation::SetPolicy); // Default forceRules=true; override below.
    set_room_policy(a.network,{false});wait([&]{return !a.network.pendingRoomRequest;});
    request(a,RoomOperation::StartMatch);
    wait([&]{for(auto* p:{&a,&b,&c,&d})if(p->network.matchPhase!=RoomMatchPhase::Preparing)return false;return true;});
    LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.nextCount=0;setup.blockMaxWidth=4;setup.blockMaxHeight=4;
    for(auto* p:{&a,&b,&c,&d}){send(p->network,"start2");send(p->network,encode_lobby_player_setup(setup));}
    wait([&]{return a.network.playerSetups.size()==3;});
    for(auto* p:{&a,&b,&c,&d})request(*p,RoomOperation::SetReady,1);
    wait([&]{return a.network.matchPhase==RoomMatchPhase::Countdown;});
    for(auto* p:{&a,&b,&c,&d})send(p->network,"start6");
    wait([&]{return a.network.matchPhase==RoomMatchPhase::Playing;});
    const auto epoch=a.network.matchEpoch;
    manage(a,d,PlayerRole::Spectator);
    wait([&]{return roleOf(d,d.network.roomPlayerIndex)==PlayerRole::Spectator;});
    manage(a,d,PlayerRole::Battle);
    assert(a.network.matchPhase==RoomMatchPhase::Playing && a.network.matchEpoch==epoch);
    const auto eliminated=std::find_if(a.network.roomMembers.begin(),a.network.roomMembers.end(),[&](const auto& m){return m.slot==d.network.roomPlayerIndex;});
    assert(eliminated!=a.network.roomMembers.end() && !eliminated->alive); // No mid-round revival.
    Engine::initialize_multiplayer_scene(b.context);
    request(a,RoomOperation::Kick,b.network.roomPlayerIndex,b.user.username);
    wait([&]{return b.network.room.name.empty() && a.network.room.players==3;});
    assert(a.network.matchPhase==RoomMatchPhase::Playing && a.network.matchEpoch==epoch);
    bool interrupted=false;try{App::wait_for_room_ready(b.context);}catch(const App::RoomSettingsRequested&){interrupted=true;}
    assert(interrupted);
    request(a,RoomOperation::CancelMatch);
    if(server) {
        // Drop the victim's first departure datagram, then force a heartbeat.
        request_room(a.network,RoomOperation::Kick,d.user.username,false,false,d.network.roomPlayerIndex);
        const auto deadline=std::chrono::steady_clock::now()+3s;
        while(a.network.pendingRoomRequest && std::chrono::steady_clock::now()<deadline){server->poll(1ms);poll_connection(a.network);}
        assert(!a.network.pendingRoomRequest);
        while(d.network.socket->try_receive()){}
        assert(!d.network.room.name.empty());d.network.lastConnectionSend={};
        wait([&]{return d.network.room.name.empty();});
        assert(d.network.roomMessage=="You were kicked from the room.");
    }
    for(auto* p:peers)if(!p->network.room.name.empty())request(*p,RoomOperation::Leave);
    // Legacy rooms and their waiting/playing clients also receive normal Left.
    for(auto* p:{&c,&d}){disconnect_online(p->network);initialize_network_services(p->context,endpoint,3);}
    wait([&]{return c.network.connection==ConnectionState::Connected && d.network.connection==ConnectionState::Connected;});
    request(c,RoomOperation::Create,-1,name+"-legacy");request(d,RoomOperation::Join,-1,name+"-legacy");
    request(c,RoomOperation::TransferHost,d.network.roomPlayerIndex,d.user.username);
    wait([&]{return is_room_host(d.network) && !is_room_host(c.network);});
    request(c,RoomOperation::TransferHost,c.network.roomPlayerIndex,c.user.username);
    assert(c.network.roomMessage==room_result_message(RoomResult::HostOnly));
    request(d,RoomOperation::TransferHost,c.network.roomPlayerIndex,"Stale target name");
    assert(d.network.roomMessage==room_result_message(RoomResult::InvalidPlayer));
    request(d,RoomOperation::TransferHost,c.network.roomPlayerIndex,c.user.username);
    wait([&]{return is_room_host(c.network);});
    request(c,RoomOperation::Kick,d.network.roomPlayerIndex,d.user.username);
    wait([&]{return d.network.room.name.empty() && c.network.room.players==1;});
    assert(d.network.connection==ConnectionState::Connected);
    interrupted=false;try{receive_lobby_signal_for(d.context,0ms);}catch(const LobbyInterrupted&){interrupted=true;}
    assert(interrupted);request(c,RoomOperation::Leave);
    std::cout<<"room_kick_test: automatic offline, own/host status restoration, battle capacity, permissions, rejoin, duplicate requests, active round and legacy clients passed; notification loss injection "<<(server?"passed locally":"skipped on public endpoint")<<"\n";
}
