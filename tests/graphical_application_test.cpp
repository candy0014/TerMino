#include "tetr/frontend/graphical/session.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/map_store.h"
#include "tetr/config/presets.h"
#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "input.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace {
using namespace std::chrono_literals;
using Id=UserConfig::SettingId;
const char* phase="start";
void stage(const char* name) {phase=name;std::cerr<<"Application test: "<<name<<std::endl;}
std::function<void()> pump=[]{};
template<class Predicate> Graphical::SessionView wait(Graphical::Session& session,Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+10s;
    for(;;) {
        pump();
        auto view=session.view();if(predicate(view))return view;
        if(view.status==Graphical::SessionStatus::Error || std::chrono::steady_clock::now()>=deadline) {
            std::cerr<<"Failed in "<<phase<<": status="<<static_cast<int>(view.status)<<" peers="<<view.peers.size()<<" pieces="<<view.board.pieces<<" "<<view.message;
            if(view.ui) {std::cerr<<" UI="<<view.ui->metadata.id<<" path="<<view.ui->metadata.path<<" selected="<<view.ui->metadata.selected;
                for(const auto& entry:view.ui->metadata.entries)std::cerr<<" ["<<entry.id<<"]";}
            std::cerr<<'\n';assert(false);
        }
        std::this_thread::sleep_for(2ms);
    }
}
void tap(Graphical::Session& session,SDL_Scancode key) {
    const int native=Graphical::native_key(key);assert(native>0);
    session.input().update(native,true);std::this_thread::sleep_for(25ms);
    session.input().update(native,false);std::this_thread::sleep_for(30ms);
}
auto ui(Graphical::Session& s,const std::string& id) {
    return wait(s,[&](const auto& v){return v.ui && v.ui->metadata.id==id;});
}
void choose(Graphical::Session& s,const std::string& id) {
    for(int i=0;i<200;++i) {
        const auto v=s.view();assert(v.ui);const auto& m=v.ui->metadata;
        const auto found=std::find_if(m.entries.begin(),m.entries.end(),[&](const auto& e){return e.id==id;});
        if(found==m.entries.end()) {std::cerr<<"Missing entry "<<id<<" in "<<m.id<<'\n';assert(false);}
        assert(found->enabled);
        if(m.selected==static_cast<std::size_t>(found-m.entries.begin())) {tap(s,SDL_SCANCODE_RETURN);return;}
        tap(s,SDL_SCANCODE_DOWN);
    }
    assert(false);
}
std::string setting(Id id) {return "setting-"+std::to_string(static_cast<int>(id));}
void text(Graphical::Session& s,const std::string& value) {
    wait(s,[&](const auto&){return s.text_input_active();});
    s.text_event({Frontend::TextEditKind::Insert,value});s.text_event({Frontend::TextEditKind::Confirm});
    wait(s,[&](const auto&){return !s.text_input_active();});
}
UserConfig::Settings defaults() {
    UserConfig::Settings s;s.keyCodes=UserConfig::default_key_codes();s.uiLanguage=1;s.gravityInterval=10;return s;
}
void root(Graphical::Session& s) {
    for(int i=0;i<10;++i) {const auto v=s.view();if(v.ui && v.ui->metadata.id=="root")return;tap(s,SDL_SCANCODE_LEFT);}
    assert(false);
}
}
int main() {
    const auto original=std::filesystem::current_path();
    const auto directory=std::filesystem::temp_directory_path()/("termino-application-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);std::filesystem::current_path(directory);
    auto first=defaults(),second=defaults();first.persistencePath="player1.ini";second.persistencePath="player2.ini";
    UserConfig::save(first);UserConfig::save(second);
    {
        Graphical::Session s(first,second,{});ui(s,"start");
        stage("start keyboard focus");
        s.input().update(Graphical::native_key(SDL_SCANCODE_RSHIFT),true);tap(s,SDL_SCANCODE_TAB);
        s.input().update(Graphical::native_key(SDL_SCANCODE_RSHIFT),false);assert(s.view().ui->metadata.selected==2);
        tap(s,SDL_SCANCODE_TAB);assert(s.view().ui->metadata.selected==0);
        tap(s,SDL_SCANCODE_TAB);assert(s.view().ui->metadata.selected==1);
        tap(s,SDL_SCANCODE_RETURN);ui(s,"root");
        stage("Unicode username persistence");choose(s,"NETWORK");ui(s,"NETWORK");
        choose(s,setting(Id::Username));text(s,"图形 测试");ui(s,"NETWORK");
        UserConfig::Settings saved;UserConfig::load(saved,"player1.ini");assert(saved.username=="图形 测试");
        stage("numeric field validation");root(s);choose(s,"GAME");choose(s,"playfield");
        choose(s,setting(Id::FieldWidth));text(s,"12");
        ui(s,"playfield");UserConfig::load(saved,"player1.ini");assert(saved.fieldWidth==12);
        stage("map editor create/paint/save");root(s);choose(s,"maps");choose(s,"map-new");tap(s,SDL_SCANCODE_RETURN);ui(s,"map-editor");
        tap(s,SDL_SCANCODE_SPACE);tap(s,SDL_SCANCODE_S);
        UserConfig::Maps::Collection maps;assert(UserConfig::Maps::load(maps) && maps.maps.size()==1);
        tap(s,SDL_SCANCODE_ESCAPE);ui(s,"maps");
        stage("preset editor save");root(s);choose(s,"presets");choose(s,"preset-new");
        ui(s,"root");choose(s,"preset-rename");text(s,"中文预设");ui(s,"root");
        choose(s,"preset-save");
        UserConfig::Presets::Collection presets;assert(UserConfig::Presets::load(presets));
        assert(std::any_of(presets.presets.begin(),presets.presets.end(),[](const auto& p){return p.name=="中文预设";}));
        tap(s,SDL_SCANCODE_ESCAPE);ui(s,"presets");
        stage("key capture and binding persistence");root(s);choose(s,"CONTROLS");choose(s,setting(Id::ControlHardDrop));
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::KeyCapture;});
        tap(s,SDL_SCANCODE_F7);ui(s,"CONTROLS");UserConfig::load(saved,"player1.ini");
        const auto drops=UserConfig::control_keys(saved,3);
        assert(std::find(drops.begin(),drops.end(),Graphical::native_key(SDL_SCANCODE_F7))!=drops.end());
        stage("challenge catalog");root(s);choose(s,"challenges");
        auto catalog=ui(s,"challenges");assert(!catalog.ui->metadata.entries.empty());
        stage("full app gameplay and menu return");tap(s,SDL_SCANCODE_R);
        wait(s,[](const auto& v){return !v.ui && v.status==Graphical::SessionStatus::Playing && v.board.width==12;});
        tap(s,SDL_SCANCODE_C);tap(s,SDL_SCANCODE_F7);
        wait(s,[](const auto& v){return v.board.pieces==1 && v.board.hold.valid();});
        tap(s,SDL_SCANCODE_U);wait(s,[](const auto& v){return v.board.pieces==0;});
        tap(s,SDL_SCANCODE_F2);wait(s,[](const auto& v){return v.status==Graphical::SessionStatus::Paused;});
        tap(s,SDL_SCANCODE_F2);tap(s,SDL_SCANCODE_ESCAPE);ui(s,"start");
    }
    stage("Bot through original full client loop");
    first=defaults();first.autoplayMode=1;first.autoplayPps=20;first.goal=4;first.pieceTarget=2;
    Graphical::LaunchOptions play;play.startScreen=false;
    {Graphical::Session s(first,second,play);wait(s,[](const auto& v){return v.status==Graphical::SessionStatus::Finished && v.board.pieces==2;});
        stage("statistics and original save dialog");tap(s,SDL_SCANCODE_TAB);
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::Statistics;});
        tap(s,SDL_SCANCODE_S);text(s,"中文录像.txt");
        wait(s,[](const auto&){return std::filesystem::exists(std::filesystem::u8path(".tetr/replay/中文录像.txt"));});
    }
    stage("full client loop replay with Unicode path");
    first=defaults();first.gameMode=5;first.replayPath=".tetr/replay/中文录像.txt";
    {Graphical::Session s(first,second,play);
        wait(s,[](const auto& v){return v.replay && v.status==Graphical::SessionStatus::Finished && v.board.pieces==2;});
    }
    stage("local versus two independently controlled boards");
    first=defaults();second=defaults();first.gameMode=6;second.gameMode=6;
    second.keyCodes[3]=Graphical::native_key(SDL_SCANCODE_KP_ENTER);
    {Graphical::Session s(first,second,play);
        wait(s,[](const auto& v){return v.status==Graphical::SessionStatus::Playing && v.peers.size()==1 && v.peers[0].board.active;});
        tap(s,SDL_SCANCODE_SPACE);tap(s,SDL_SCANCODE_KP_ENTER);
        wait(s,[](const auto& v){return v.board.pieces==1 && v.peers.size()==1 && v.peers[0].board.pieces==1;});
    }
    stage("graphical room UI and original terminal peer");
    {
        Network::RelayServer relay(341);relay.bind(3197);std::atomic<bool> relayRunning=true;
        std::thread relayThread([&]{while(relayRunning)relay.poll(1ms);});
        struct RelayStop {std::atomic<bool>& running;std::thread& thread;~RelayStop(){running=false;thread.join();}} relayStop{relayRunning,relayThread};
        auto state=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
        Runtime::GameContext context(*state,services);Network::NetworkServices network;Input::Services input;
        Input::FocusState focus;Replay::ReplaySessionState replay;App::ApplicationState app;
        auto peer=defaults();peer.gameMode=4;peer.username="Terminal peer";peer.serverIp="127.0.0.1:3197";
        context.attach_user_settings(peer);context.attach_input(input);context.attach_network(network);context.attach_app(app);context.attach_replay(replay);
        app.primaryContext=&context;app.headlessLocalMatch=true;
        input.initialized=true;input.focusSource=&focus;input.keyEventSource=&focus.events();Input::refresh_bindings(input,peer.keyCodes);
        first=defaults();first.serverIp=peer.serverIp;first.username="Graphical host";
        Graphical::Session s(first,second,{});ui(s,"start");tap(s,SDL_SCANCODE_P);ui(s,"root");choose(s,"NETWORK");choose(s,"connect");
        const auto has=[&](const auto& v,const std::string& id){return v.ui && std::any_of(v.ui->metadata.entries.begin(),v.ui->metadata.entries.end(),[&](const auto& e){return e.id==id && e.enabled;});};
        wait(s,[&](const auto& v){return has(v,"room-create");});choose(s,"room-create");ui(s,"rooms");
        wait(s,[&](const auto& v){return has(v,"name");});choose(s,"name");text(s,"GLUI");ui(s,"rooms");choose(s,"submit");
        wait(s,[&](const auto& v){return has(v,"room-users");});
        Network::initialize_network_services(context,peer.serverIp);
        pump=[&]{Network::poll_connection(network);};
        wait(s,[&](const auto&){return network.connection==Network::ConnectionState::Connected;});
        Network::request_room(network,Network::RoomOperation::Join,"GLUI");
        wait(s,[&](const auto&){return !network.pendingRoomRequest && network.room.players==2;});
        choose(s,"room-users");
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.id=="rooms" && v.ui->metadata.entries.size()==3;});
        tap(s,SDL_SCANCODE_LEFT);ui(s,"NETWORK");choose(s,"room-start");
        pump=[]{};
        std::atomic<bool> stopPeer=false,peerReady=false,canStart=false,peerFailed=false;
        struct StopPeer {};
        const auto observePeer=[&]{
            if(stopPeer)throw StopPeer{};
            const auto ready=std::count_if(network.roomMembers.begin(),network.roomMembers.end(),[](const auto& member){return member.ready;});
            if(ready==2)canStart=true;
        };
        services.clock.waitInterrupt=observePeer;
        input.controlInterrupt=observePeer;
        std::thread terminal([&]{try {App::initialize_game(context);peerReady=true;App::run(context);}catch(const StopPeer&){}catch(const std::exception& e){std::cerr<<"Terminal peer: "<<e.what()<<'\n';peerFailed=true;}});
        struct PeerStop {std::atomic<bool>& stop;std::thread& thread;~PeerStop(){stop=true;thread.join();}} peerStop{stopPeer,terminal};
        stage("mixed frontend match start");
        // Both fighters must be ready before the host confirms. Setup and
        // countdown acknowledgements continue through the original protocol.
        wait(s,[&](const auto&){return canStart.load();});tap(s,SDL_SCANCODE_RETURN);
        stage("mixed frontend countdown completion");
        wait(s,[&](const auto& v){return v.status==Graphical::SessionStatus::Playing && !v.ui && !v.peers.empty() && peerReady.load();});
        stage("mixed frontend board propagation");
        focus.update(peer.keyCodes[3],true);std::this_thread::sleep_for(30ms);focus.update(peer.keyCodes[3],false);
        tap(s,SDL_SCANCODE_SPACE);
        wait(s,[](const auto& v){return v.board.pieces==1 && !v.peers.empty() && v.peers[0].board.pieces==1;});
        assert(!peerFailed);
        stage("mixed frontend clean shutdown");
    }
    std::filesystem::current_path(original);std::filesystem::remove_all(directory);
    std::cerr<<"Full graphical application: settings/Unicode/maps/presets/key-capture/challenges/Bot/statistics/save/local-versus/mixed-frontend-network OK\n";
}
