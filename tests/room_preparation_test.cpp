#include "tetr/app/application_state.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/config/normalization.h"
#include "tetr/config/multiplayer_config.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/input/services.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/round_statistics.h"
#include "tetr/network/transport.h"
#include "tetr/runtime/game_context.h"
#include "../src/app/multiplayer_battle.h"
#include "../src/app/settings_navigation.h"
#include "../src/app/terminal_canvas.h"
#include "../src/app/ui_text.h"
#include "support/terminal_screen.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <linux/input.h>
#endif
#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace TetrTests::room_preparation_test {
using namespace Network;
using namespace std::chrono_literals;
struct Player {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    UserConfig::Settings user;Input::Services input;NetworkServices network;App::ApplicationState app;
    std::set<int> held;
    Player() {
        context.attach_network(network);context.attach_input(input);context.attach_user_settings(user);context.attach_app(app);
        context.attach_output_mutex(app.terminalOutputMutex);app.primaryContext=&context;
        user.gameMode=4;user.keyCodes=UserConfig::default_key_codes();input.initialized=true;
        input.keyStateProvider=[&](int key){return held.count(key)!=0;};Input::refresh_bindings(input,user.keyCodes);
        context.config=Runtime::normalize_user_config(user,Runtime::GameMode::Multiplayer);
        auto& p=session->player;p.gameplay.playfield.width=10;p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=20;
        p.gameplay.playfield.field.initialize(10,20);p.pieces.bag.push_back({Piece::Type{0},false});p.pieces.nextPreview={Piece::Type{1}};
    }
    ~Player(){disconnect_online(network);}
};
int run(int argc,char** argv) {
    std::unique_ptr<RelayServer> server;
    if(argc==1) {server=std::make_unique<RelayServer>(612);server->bind(3034);}
    std::atomic<bool> running=true,pauseRelay=false,relayPaused=false;
    std::thread worker;
    if(server)worker=std::thread([&]{while(running) {
        if(pauseRelay) {relayPaused=true;std::this_thread::sleep_for(1ms);continue;}
        relayPaused=false;server->poll(1ms);
    }});
    struct Stop {std::atomic<bool>& running;std::thread& worker;~Stop(){running=false;if(worker.joinable())worker.join();}} stop{running,worker};
    Player host,first,second,offline;std::vector<Player*> players{&host,&first,&second,&offline};
    host.user.username="Host";first.user.username="First";second.user.username="Second";offline.user.username="Offline";
    const auto pump=[&]{for(auto* p:players)poll_connection(p->network);std::this_thread::sleep_for(1ms);};
    int waitStep=0;
    const auto wait=[&](auto done){
        ++waitStep;const auto end=std::chrono::steady_clock::now()+(server?5s:15s);
        while(!done() && std::chrono::steady_clock::now()<end)pump();
        if(!done()) {
            std::cerr<<"Preparation timeout at wait "<<waitStep<<'\n';
            for(auto* p:players) {
                std::cerr<<p->user.username<<" phase="<<static_cast<int>(p->network.matchPhase)<<" epoch="<<p->network.matchEpoch
                    <<" message="<<p->network.roomMessage<<" members=";
                for(const auto& m:p->network.roomMembers)std::cerr<<m.username<<'/'<<static_cast<int>(m.role)<<'/'<<m.ready<<'/'<<m.participating<<'/'<<m.host<<',';
                std::cerr<<'\n';
            }
        }
        assert(done());
    };
    const auto request=[&](Player& p,RoomOperation op,int value=-1,const std::string& name="") {
        if(!server)std::cerr<<p.user.username<<" operation="<<static_cast<int>(op)<<" target="<<value<<" next wait="<<waitStep+1<<'\n';
        request_room(p.network,op,name,false,false,value);wait([&]{return !p.network.pendingRoomRequest;});
    };
    const auto self=[](const Player& p)->const RoomMember& {
        for(const auto& m:p.network.roomMembers)if(m.slot==p.network.roomPlayerIndex)return m;
        throw std::runtime_error("Missing self in room roster");
    };
    const auto all_see=[&](Player& target,bool ready) {
        return std::all_of(players.begin(),players.end(),[&](const auto* p){
            for(const auto& m:p->network.roomMembers)if(m.slot==target.network.roomPlayerIndex)return m.ready==ready;
            return false;
        });
    };
    for(auto* p:players)initialize_network_services(p->context,argc>1?argv[1]:"127.0.0.1:3034");
    wait([&]{for(auto* p:players)if(p->network.connection!=ConnectionState::Connected)return false;return true;});
    request(host,RoomOperation::Create,-1,"Manual-preparation-"+host.network.connectionToken.substr(0,8));
    for(auto* p:{&first,&second,&offline})request(*p,RoomOperation::Join,-1,host.network.room.name);
    set_room_policy(host.network,{false});wait([&]{return !host.network.pendingRoomRequest;});
    // Three or four ready fighters can start in a larger room. Confirmation
    // may arrive before the final fighter finishes uploading its setup.
    for(bool forceRules:{false,true}) {
      set_room_policy(host.network,{forceRules});wait([&]{return !host.network.pendingRoomRequest;});
      host.network.localRoomConfig=UserConfig::encode_multiplayer_config(host.user,host.context.config);
      for(int count:{3,4}) {
        for(int i=0;i<count;++i)request(*players[i],RoomOperation::Prepare);
        wait([&]{for(int i=0;i<4;++i)if(!all_see(*players[i],i<count))return false;return true;});
        if(forceRules) {
            wait([&]{for(int i=0;i<count;++i)if(!players[i]->network.roomConfig.incoming.payload)return false;return true;});
            for(int i=0;i<count-1;++i)accept_room_config(players[i]->network);
        }
        request(host,RoomOperation::ForceStart);
        assert(host.network.roomMessage==room_result_message(RoomResult::MatchForced));
        LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.nextCount=1;setup.nextPreview={Piece::Type{1}};
        for(int i=0;i<count-1;++i) {
            send(players[i]->network,"start2");send(players[i]->network,encode_lobby_player_setup(setup));
        }
        const auto pendingUntil=std::chrono::steady_clock::now()+80ms;
        while(std::chrono::steady_clock::now()<pendingUntil)pump();
        assert(host.network.matchPhase==RoomMatchPhase::Preparing);
        if(forceRules)accept_room_config(players[count-1]->network);
        send(players[count-1]->network,"start2");send(players[count-1]->network,encode_lobby_player_setup(setup));
        wait([&]{return std::all_of(players.begin(),players.end(),[](const auto* p){return p->network.matchPhase==RoomMatchPhase::Countdown;});});
        for(int i=0;i<4;++i)assert(self(*players[i]).participating==(i<count));
        const auto epoch=host.network.matchEpoch;
        request(host,RoomOperation::TransferHost,first.network.roomPlayerIndex,"First");
        wait([&]{return is_room_host(first.network);});
        assert(host.network.matchEpoch==epoch && host.network.matchPhase==RoomMatchPhase::Countdown);
        request(first,RoomOperation::TransferHost,host.network.roomPlayerIndex,"Host");
        wait([&]{return is_room_host(host.network);});
        request(host,RoomOperation::CancelMatch);
      }
    }
    set_room_policy(host.network,{false});wait([&]{return !host.network.pendingRoomRequest;});
    request(host,RoomOperation::SetRole,2);request(second,RoomOperation::SetRole,1);request(offline,RoomOperation::SetRole,2);
    // Hold the reply so the guest's Prepare really is queued behind SetRole,
    // independent of loopback/socket scheduling on Linux and Windows.
    if(server) {pauseRelay=true;wait([&]{return relayPaused.load();});}
    request_room(first.network,RoomOperation::SetRole,{},true,false,0);
    request_room(first.network,RoomOperation::Prepare);
    assert(first.network.queuedMatchControl==RoomOperation::Prepare);
    pauseRelay=false;
    wait([&]{return all_see(first,true);});
    assert(self(host).role==PlayerRole::Offline && !self(host).ready);
    assert(!self(second).ready && !self(offline).ready);
    // A guest enters before the host. Its ready state is real even before
    // configuration/setup exchange, and propagates to every connected viewer.
    assert(first.network.matchPhase==RoomMatchPhase::Preparing);
    request(host,RoomOperation::Prepare);wait([&]{return self(host).role==PlayerRole::Spectator;});
    assert(!self(host).ready);
    request(host,RoomOperation::ForceStart);assert(host.network.roomMessage==room_result_message(RoomResult::NeedReadyPlayers));
    request(second,RoomOperation::SetRole,0);request(second,RoomOperation::Prepare);
    wait([&]{return all_see(second,true);});
    request(first,RoomOperation::SetReady,0);wait([&]{return all_see(first,false);});
    request(first,RoomOperation::Prepare);wait([&]{return all_see(first,true);});
    // Host management remains available after entering preparation, including
    // transfer to an offline member; readiness survives the new rules epoch.
    auto transferEpoch=host.network.matchEpoch;
    request(host,RoomOperation::TransferHost,offline.network.roomPlayerIndex,"Offline");
    assert(host.network.roomMessage==room_result_message(RoomResult::HostTransferred));
    wait([&]{return is_room_host(offline.network) && host.network.matchEpoch!=transferEpoch && all_see(first,true) && all_see(second,true);});
    request(offline,RoomOperation::TransferHost,host.network.roomPlayerIndex,"Host");
    wait([&]{return is_room_host(host.network) && all_see(first,true) && all_see(second,true);});
    const auto noAutoStart=std::chrono::steady_clock::now()+120ms;
    while(std::chrono::steady_clock::now()<noAutoStart)pump();
    assert(host.network.matchPhase==RoomMatchPhase::Preparing);
    auto beforeSettings=host.network.matchEpoch;
    assert(handle_room_game_control(host.context,false));
    wait([&]{return host.network.matchEpoch!=beforeSettings && all_see(first,true) && all_see(second,true);});
    request(host,RoomOperation::Prepare);
    set_room_policy(host.network,{true});wait([&]{return !host.network.pendingRoomRequest && host.network.roomPolicy.forceRules;});
    wait([&]{return all_see(first,true) && all_see(second,true);});
    set_room_policy(host.network,{false});wait([&]{return !host.network.pendingRoomRequest && !host.network.roomPolicy.forceRules;});
    wait([&]{return all_see(first,true) && all_see(second,true);});
    App::UiText::set_language(App::UiText::Language::English);
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());App::TerminalCanvas canvas;std::size_t offset=0;
    App::paint_room_waiting(host.context,canvas,{120,45},0,offset);
    std::cout.rdbuf(previous);Test::TerminalScreen screen;screen.feed(output.str());const auto text=screen.text();
    assert(text.find("Start match now")!=std::string::npos && text.find("Cancel match")==std::string::npos);
    assert(text.find("Battle / Ready")!=std::string::npos && text.find("Spectator")!=std::string::npos && text.find("Offline")!=std::string::npos);
    const auto setups=[&] {
        // Readiness survives policy/host changes, so all_see(..., true) is
        // not an epoch barrier. Wait for every test actor to receive the new
        // room state before sending setup packets tagged with its local epoch.
        wait([&]{return std::all_of(players.begin(),players.end(),[&](const auto* p) {
            return p->network.matchEpoch==host.network.matchEpoch && p->network.roomPolicy==host.network.roomPolicy;
        });});
        LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.nextCount=1;setup.nextPreview={Piece::Type{1}};setup.hybridBoardSync=true;
        for(auto* p:players)if(self(*p).role!=PlayerRole::Offline) {
            p->network.localPlayerSetup=encode_lobby_player_setup(setup);
            send(p->network,"start2");send(p->network,p->network.localPlayerSetup);
        }
    };
    const auto finish_countdown=[&] {
        wait([&]{return host.network.matchPhase==RoomMatchPhase::Countdown;});
        for(auto* p:players)if(self(*p).participating)send(p->network,"start6");
        wait([&]{for(auto* p:players)if(p->network.matchPhase!=RoomMatchPhase::Playing)return false;return true;});
        for(auto* p:players)Engine::initialize_multiplayer_scene(p->context);
    };
    // One explicit confirmation can wait for setup; no second confirmation
    // and no permanently-unready local player is needed after it completes.
    request(host,RoomOperation::ForceStart);assert(host.network.matchPhase==RoomMatchPhase::Preparing);
    setups();finish_countdown();
    const auto firstEpoch=host.network.matchEpoch;
    request(host,RoomOperation::TransferHost,first.network.roomPlayerIndex,"First");
    wait([&]{return is_room_host(first.network);});
    assert(host.network.matchEpoch==firstEpoch && host.network.matchPhase==RoomMatchPhase::Playing);
    assert(self(first).alive && self(second).alive && self(first).ready && self(second).ready);
    request(first,RoomOperation::TransferHost,host.network.roomPlayerIndex,"Host");
    wait([&]{return is_room_host(host.network);});
    send_round_statistics(first.context,first.session->player);
    wait([&]{return !first.network.pendingResultEpoch;});
    assert(handle_room_game_control(first.context,true));
    wait([&]{return host.network.matchPhase==RoomMatchPhase::Finished;});
    wait([&]{return all_see(first,false) && all_see(second,false);});
    assert(host.network.matchEpoch==firstEpoch);
    request(first,RoomOperation::Prepare);
    wait([&]{return host.network.matchEpoch!=firstEpoch && all_see(first,true);});
    send_round_statistics(first.context,first.session->player);assert(first.network.pendingResultEpoch==firstEpoch);
    wait([&]{return !first.network.pendingResultEpoch;});
    assert(!self(second).ready && !self(host).ready); // Still reading results.
    request(second,RoomOperation::Prepare);request(host,RoomOperation::Prepare);
    handle_room_game_control(host.context,false);
    const auto oldNames=host.network.roundResultNames[firstEpoch];
    wait([&]{return all_see(first,true) && all_see(second,true) && !self(host).ready;});
    request(host,RoomOperation::Prepare);
    host.network.roundResultWires[firstEpoch].erase(first.network.roomPlayerIndex);
    request_round_statistics(host.network,firstEpoch,first.network.roomPlayerIndex);
    wait([&]{return host.network.roundResultWires[firstEpoch].count(first.network.roomPlayerIndex)!=0;});
    assert(host.network.roundResultNames[firstEpoch]==oldNames);
    setups();request(host,RoomOperation::ForceStart);finish_countdown();
    const auto secondEpoch=host.network.matchEpoch;
    request(first,RoomOperation::EndMatch);assert(first.network.roomMessage==room_result_message(RoomResult::HostOnly));
#ifdef _WIN32
    const std::set<int> stopChord{VK_LCONTROL,VK_LSHIFT,VK_F12};
#else
    const std::set<int> stopChord{KEY_LEFTCTRL,KEY_LEFTSHIFT,KEY_F12};
#endif
    App::install_start_screen_navigation(first.context);first.held=stopChord;first.input.poll_control_interrupt();first.held.clear();
    assert(first.network.matchEpoch==secondEpoch);
    App::install_start_screen_navigation(host.context);host.held=stopChord;
    bool interrupted=false;try {host.input.poll_control_interrupt();}catch(const LobbyInterrupted&){interrupted=true;}
    host.held.clear();assert(interrupted);
    wait([&]{return host.network.matchEpoch!=secondEpoch && host.network.matchPhase==RoomMatchPhase::Preparing && all_see(first,true) && all_see(second,true);});
    assert(!self(host).ready && !self(offline).ready);
    // Four battle actors exercise all three exit paths while another pair
    // continues playing. The host's Retry must not restart other players.
    request(host,RoomOperation::SetRole,0);request(offline,RoomOperation::SetRole,0);request(offline,RoomOperation::Prepare);
    setups();wait([&]{return all_see(host,true) && all_see(offline,true);});
    Engine::initialize_multiplayer_scene(host.context);
    std::ostringstream liveOutput;previous=std::cout.rdbuf(liveOutput.rdbuf());Test::TerminalScreen liveScreen;
    std::size_t consumed=0;int liveStage=0;const auto liveDeadline=std::chrono::steady_clock::now()+5s;
    const auto savedProvider=host.input.keyStateProvider;const auto savedInterrupt=host.input.controlInterrupt;
    host.input.controlInterrupt={};
    host.input.keyStateProvider=[&](int key) {
        assert(std::chrono::steady_clock::now()<liveDeadline);
        for(auto* p:{&first,&second,&offline})poll_connection(p->network);
        const auto bytes=liveOutput.str();if(bytes.size()>consumed){liveScreen.feed(bytes.substr(consumed));consumed=bytes.size();}
        const auto visible=liveScreen.text();
        if(liveStage==0 && visible.find("Ready 4/4")!=std::string::npos) {
            request_room(second.network,RoomOperation::SetReady,{},true,false,0);liveStage=1;
        } else if(liveStage==1 && visible.find("Ready 3/4")!=std::string::npos) {
            request_room(second.network,RoomOperation::Prepare);liveStage=2;
        } else if(liveStage==2 && visible.find("Ready 4/4")!=std::string::npos &&
                  key==App::Settings::NavigationInput::key_code(App::Settings::NavigationKey::Confirm)) {
            liveStage=3;return true;
        }
        return false;
    };
    try {assert(App::wait_for_room_ready(host.context));}catch(...){std::cout.rdbuf(previous);throw;}
    host.input.keyStateProvider=savedProvider;host.input.controlInterrupt=savedInterrupt;std::cout.rdbuf(previous);
    assert(liveStage==3);finish_countdown();const auto finalEpoch=host.network.matchEpoch;
    assert(handle_room_game_control(second.context,false));
    wait([&]{return !self(second).alive;});assert(host.network.matchPhase==RoomMatchPhase::Playing);
    assert(handle_room_game_control(host.context,true));assert(host.session->roomScene.localForfeit && !host.session->roomScene.selfAlive);
    Engine::update_multiplayer_scene(host.context);assert(!host.session->roomScene.selfAlive);
    assert(std::find(host.session->roomScene.survivors.begin(),host.session->roomScene.survivors.end(),host.network.roomPlayerIndex)==host.session->roomScene.survivors.end());
    wait([&]{return !self(host).alive;});assert(host.network.matchPhase==RoomMatchPhase::Playing && host.network.matchEpoch==finalEpoch);
    bool escaped=false;try {App::request_start_screen(first.context);}catch(const App::StartScreenRequested&){escaped=true;}
    assert(escaped && first.app.startScreenRequested);
    wait([&]{return host.network.matchPhase==RoomMatchPhase::Finished;});
    assert(host.network.matchEpoch==finalEpoch);
    wait([&]{for(auto* p:players)if(!all_see(*p,false))return false;return true;});
    std::cerr<<"room_preparation_test: live roster, immediate readiness, manual start, results, host stop and all exit keys OK\n";
    return 0;
}
} // namespace TetrTests::room_preparation_test
