#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/config/multiplayer_config.h"
#include "tetr/config/normalization.h"
#include "tetr/input/services.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/transport.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "../src/app/settings_navigation.h"
#include "../src/app/ui_text.h"
#include "support/network_gameplay_frames.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace TetrTests::room_late_spectator_test {
using namespace Network;
using namespace std::chrono_literals;
struct Player {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    UserConfig::Settings user;NetworkServices network;Input::Services input;
    Replay::ReplaySessionState replay;App::ApplicationState app;
    explicit Player(const std::string& address) {
        context.attach_network(network);context.attach_input(input);context.attach_user_settings(user);
        context.attach_replay(replay);context.attach_app(app);context.attach_output_mutex(app.terminalOutputMutex);
        app.primaryContext=&context;app.headlessLocalMatch=true;
        user.gameMode=4;user.gravityInterval=-1;user.nextCount=2;user.uiLanguage=1;user.serverIp=address;
        user.keyCodes=UserConfig::default_key_codes();input.initialized=true;input.keyStateProvider=[](int){return false;};
        Input::refresh_bindings(input,user.keyCodes);context.config=Runtime::normalize_user_config(user,Runtime::GameMode::Multiplayer);
        initialize_network_services(context,address);
    }
    ~Player(){disconnect_online(network);}
};
int run(int argc,char** argv) {
    std::unique_ptr<RelayServer> server;
    if(argc==1) {server=std::make_unique<RelayServer>(635);server->bind(3035);}
    std::atomic<bool> running=true;std::thread worker;
    if(server)worker=std::thread([&]{while(running)server->poll(1ms);});
    struct Stop {std::atomic<bool>& running;std::thread& worker;~Stop(){running=false;if(worker.joinable())worker.join();}} stop{running,worker};
    const std::string address=argc>1?argv[1]:"127.0.0.1:3035";
    Player host(address),fighter(address),spectator(address);std::vector<Player*> players{&host,&fighter,&spectator};
    const auto pump=[&]{for(auto* p:players)poll_connection(p->network);std::this_thread::sleep_for(1ms);};
    const auto wait=[&](const auto& done) {
        const auto until=std::chrono::steady_clock::now()+15s;
        while(!done() && std::chrono::steady_clock::now()<until)pump();
        assert(done());
    };
    const auto request=[&](Player& p,RoomOperation operation,int value=-1,const std::string& name="") {
        request_room(p.network,operation,name,false,false,value);wait([&]{return !p.network.pendingRoomRequest;});
    };
    wait([&]{for(auto* p:players)if(p->network.connection!=ConnectionState::Connected)return false;return true;});
    const auto room="Late-spectator-"+host.network.connectionToken.substr(0,8);
    request(host,RoomOperation::Create,-1,room);
    for(auto* p:{&fighter,&spectator})request(*p,RoomOperation::Join,-1,room);
    request(spectator,RoomOperation::SetRole,1);
    App::UiText::set_language(App::UiText::Language::English);
    for(bool forceRules:{false,true}) {
        if(host.network.matchPhase!=RoomMatchPhase::Idle)request(host,RoomOperation::CancelMatch);
        set_room_policy(host.network,{forceRules});wait([&]{return !host.network.pendingRoomRequest;});
        host.network.localRoomConfig=UserConfig::encode_multiplayer_config(host.user,host.context.config);
        request(host,RoomOperation::Prepare);request(fighter,RoomOperation::Prepare);
        const auto epoch=host.network.matchEpoch;
        if(forceRules) {
            wait([&]{return host.network.roomConfig.incoming.payload && fighter.network.roomConfig.incoming.payload;});
            for(auto* p:{&host,&fighter})accept_room_config(p->network);
        }
        LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.nextCount=2;setup.holdEnabled=true;
        setup.nextPreview={Piece::Type{1},Piece::Type{2}};setup.blockMaxWidth=setup.blockMaxHeight=4;
        for(auto* p:{&host,&fighter}) {send(p->network,"start2");send(p->network,encode_lobby_player_setup(setup));}
        wait([&]{return host.network.playerSetups.count(fighter.network.roomPlayerIndex) && fighter.network.playerSetups.count(host.network.roomPlayerIndex);});
        request(host,RoomOperation::ForceStart);
        wait([&]{return host.network.matchPhase==RoomMatchPhase::Countdown;});
        for(auto* p:{&host,&fighter})send(p->network,"start6");
        wait([&]{for(auto* p:players)if(p->network.matchPhase!=RoomMatchPhase::Playing || p->network.matchEpoch!=epoch)return false;return true;});
        // This observer did not enter preparation. Retry from a menu must
        // join the live round, including with forced host configuration.
        for(int entry=0;entry<2;++entry) {
            for(auto* p:{&host,&fighter}) {
                const auto pose=Test::prepare_online_board(p->context,entry);
                send_board_state_frame(p->context,p->session->player,pose,{},true);
            }
            spectator.app.startScreenRequested=false;
            spectator.input.keyStateProvider=[](int){return false;};
            App::install_start_screen_navigation(spectator.context);
            std::ostringstream output;auto* old=std::cout.rdbuf(output.rdbuf());
            int retryReads=0;bool sawBoards=false,escaped=false,watching=false,queuedMovement=false;
            Input::KeyEventBuffer events;spectator.input.keyEventSource=&events;
            auto watchStarted=std::chrono::steady_clock::time_point{};
            auto paintStarted=std::chrono::steady_clock::time_point{};
            auto lastPeerPoll=std::chrono::steady_clock::time_point{};
            Runtime::TimePoint lastPaint{};int paintedFrames=0;
            const auto until=std::chrono::steady_clock::now()+15s;
            spectator.input.keyStateProvider=[&](int key) {
                assert(std::chrono::steady_clock::now()<until);
                const auto now=std::chrono::steady_clock::now();
                if(now-lastPeerPoll>=5ms) {
                    poll_connection(host.network);poll_connection(fighter.network);lastPeerPoll=now;
                }
                if(watching && spectator.session->roomScene.lastPaint!=lastPaint) {
                    if(!paintedFrames)paintStarted=now;
                    lastPaint=spectator.session->roomScene.lastPaint;++paintedFrames;
                }
                if(!sawBoards) {
                    const auto text=output.str();
                    sawBoards=text.find("HOLD")!=std::string::npos && text.find("NEXT")!=std::string::npos;
                }
                if(watching && !queuedMovement) {
                    queuedMovement=true;
                    // Spectator controls must not leave a captured movement
                    // event blocking all later board reception and painting.
                    events.update(spectator.user.keyCodes[1],true);
                    events.update(spectator.user.keyCodes[1],false);
                    for(auto* p:{&host,&fighter}) {
                        p->session->player.gameplay.scoring.placedBlocks=142+entry;
                        send_board_state_frame(p->context,p->session->player,
                            ActivePieceState::active({{2+entry,3+entry},Runtime::Rotation::from_index(entry)}),{},true);
                    }
                }
                if(sawBoards && key==spectator.user.keyCodes[8]) {++retryReads;return retryReads>=4 && retryReads<=8;}
                const auto received=[&](const Player& p) {
                    const auto& boards=spectator.session->roomScene.players;
                    const auto found=boards.find(p.network.roomPlayerIndex);
                    return found!=boards.end() && found->second.gameplay.scoring.placedBlocks==142+entry;
                };
                // Joining can still wait for peer setups after initialize_game.
                // Measure steady painting separately from that entry delay.
                const bool measured=forceRules || entry || (paintStarted!=std::chrono::steady_clock::time_point{} && now-paintStarted>=500ms);
                return measured && retryReads>=12 && received(host) && received(fighter) &&
                    key==App::Settings::NavigationInput::key_code(App::Settings::NavigationKey::Back);
            };
            try {
                App::initialize_game(spectator.context);
                assert(App::UiText::tr("Match start waiting")=="Match start waiting");
                assert(output.str().find("Match start waiting")==std::string::npos);
                assert(spectator.session->roomScene.spectator && !spectator.session->roomScene.selfAlive);
                watching=true;watchStarted=std::chrono::steady_clock::now();App::run(spectator.context);
            } catch(const App::StartScreenRequested&) {escaped=true;}
            catch(...) {std::cout.rdbuf(old);throw;}
            std::cout.rdbuf(old);spectator.input.keyEventSource=nullptr;spectator.input.keyStateProvider=[](int){return false;};
            assert(escaped && sawBoards && retryReads>=12 && queuedMovement);
            if(!forceRules && entry==0) {
                assert(paintedFrames>1);
                std::cerr<<"spectator paint rate: "<<(paintedFrames-1)/
                    std::chrono::duration<double>(std::chrono::steady_clock::now()-paintStarted).count()
                    <<" fps; entry delay: "<<std::chrono::duration<double,std::milli>(paintStarted-watchStarted).count()<<" ms\n";
            }
            for(auto* p:{&host,&fighter})assert(spectator.session->roomScene.players.at(p->network.roomPlayerIndex).gameplay.scoring.placedBlocks==142+entry);
            assert(output.str().find("Match start waiting")==std::string::npos);
            assert(output.str().find("Start match now")==std::string::npos);
            pump();
            for(auto* p:players) {
                assert(p->network.matchPhase==RoomMatchPhase::Playing && p->network.matchEpoch==epoch);
                for(const auto& m:p->network.roomMembers) {
                    if(m.slot==spectator.network.roomPlayerIndex)assert(m.role==PlayerRole::Spectator && !m.ready && !m.participating);
                    else assert(m.alive && m.participating);
                }
            }
        }
    }
    std::cerr<<"room_late_spectator_test: live 3-player entry/re-entry and spectator Retry, host rules on/off OK\n";
    return 0;
}
} // namespace TetrTests::room_late_spectator_test
