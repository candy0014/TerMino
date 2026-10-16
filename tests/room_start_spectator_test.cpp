#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/input/services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/transport.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "../src/app/settings_navigation.h"
#include "support/terminal_screen.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace Network;
using namespace std::chrono_literals;
struct Finished {};
struct Player {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    UserConfig::Settings user;NetworkServices network;Input::Services input;
    Replay::ReplaySessionState replay;App::ApplicationState app;
    Test::TerminalScreen screen;std::string output;
    std::array<bool,3> digits{};
    bool spectator=false,confirmSent=false,movementQueued=false;
    int fighters=0,confirmReads=0,paintedFrames=0,remoteChanges=0;
    int measurementMs=1200;
    double maxPaintGap=0,maxRemoteGap=0;
    Input::KeyEventBuffer events;
    Runtime::TimePoint lastPaint{};
    std::optional<Piece::Pose> lastRemotePose;
    std::chrono::steady_clock::time_point started{},measuredUntil{};
    std::chrono::steady_clock::time_point lastPaintObserved{},lastRemoteObserved{};
    explicit Player(int index,int count,std::recursive_mutex& outputMutex,int duration):spectator(index==count),fighters(count),measurementMs(duration) {
        context.attach_network(network);context.attach_input(input);context.attach_user_settings(user);
        context.attach_replay(replay);context.attach_app(app);context.attach_output_mutex(outputMutex);
        app.primaryContext=&context;app.headlessLocalMatch=true;
        user.gameMode=4;user.uiLanguage=1;user.gravityInterval=-1;user.nextCount=2;
        user.username=(spectator?"Observer":"Fighter-")+std::to_string(index);
        user.keyCodes=UserConfig::default_key_codes();input.initialized=true;
        input.keyStateProvider=[](int){return false;};Input::refresh_bindings(input,user.keyCodes);
        if(spectator)input.keyEventSource=&events;
    }
    ~Player(){disconnect_online(network);}
    void capture() {
        screen.feed(output);output.clear();
        if(network.matchPhase!=RoomMatchPhase::Countdown)return;
        // Read the actual digit strokes at board coordinates, independently
        // of the countdown renderer. Borders alone cannot satisfy these.
        for(int slot:Engine::full_multiplayer_slots(context)) {
            const auto& p=slot==network.roomPlayerIndex?session->player:session->roomScene.players.at(slot);
            const auto& field=p.gameplay.playfield;
            const int y=p.presentation.topMargin+2+std::min(0,field.activeHeight-field.garbageHeight-6);
            const int x=p.presentation.margin+(field.width/2)*2-(field.width%2==0)-1;
            digits[0]=digits[0] || screen.at(y+2,x+4).glyph=="┫";
            digits[1]=digits[1] || screen.at(y+2,x).glyph=="┏";
            digits[2]=digits[2] || screen.at(y+4,x+2).glyph=="┻";
        }
    }
    bool key(int code) {
        const auto now=std::chrono::steady_clock::now();
        int ready=0;for(const auto& m:network.roomMembers)if(m.role==PlayerRole::Battle && m.ready)++ready;
        if(is_room_host(network) && network.matchPhase==RoomMatchPhase::Preparing && ready==fighters &&
           network.playerSetups.size()==std::size_t(fighters) && network.countdownClockAvailable && !confirmSent &&
           code==App::Settings::NavigationInput::key_code(App::Settings::NavigationKey::Confirm) && ++confirmReads>=3) {
            confirmSent=true;return true;
        }
        if(network.matchPhase!=RoomMatchPhase::Playing)return false;
        if(!spectator && !session->gameStartTime)return false;
        if(started==std::chrono::steady_clock::time_point{})started=now;
        if(spectator) {
            if(!movementQueued) {
                movementQueued=true;events.update(user.keyCodes[1],true);events.update(user.keyCodes[1],false);
            }
            if(session->roomScene.lastPaint!=lastPaint) {
                if(lastPaintObserved!=std::chrono::steady_clock::time_point{})
                    maxPaintGap=std::max(maxPaintGap,std::chrono::duration<double>(now-lastPaintObserved).count());
                lastPaintObserved=now;lastPaint=session->roomScene.lastPaint;++paintedFrames;
            }
            const auto found=session->roomScene.players.find(session->roomScene.focusSlot);
            if(found!=session->roomScene.players.end() && found->second.presentation.activePose!=lastRemotePose) {
                if(lastRemoteObserved!=std::chrono::steady_clock::time_point{})
                    maxRemoteGap=std::max(maxRemoteGap,std::chrono::duration<double>(now-lastRemoteObserved).count());
                lastRemoteObserved=now;lastRemotePose=found->second.presentation.activePose;++remoteChanges;
            }
        }
        if(now-started>=std::chrono::milliseconds(measurementMs)) {measuredUntil=now;throw Finished{};}
        if(spectator)return false;
        if(now-started<50ms)return false;
        if(!session->player.gameplay.scoring.placedBlocks)return code==user.keyCodes[3];
        const auto phase=std::chrono::duration_cast<std::chrono::milliseconds>(now-started).count()/200;
        return code==user.keyCodes[phase%2?1:0];
    }
};
thread_local Player* actor=nullptr;
struct Output : std::streambuf {
    std::streambuf* previous=std::cout.rdbuf(this);
    ~Output(){std::cout.rdbuf(previous);}
    std::streamsize xsputn(const char* text,std::streamsize size) override {
        if(actor)actor->output.append(text,std::size_t(size));return size;
    }
    int overflow(int value) override {
        if(value!=EOF) {const char ch=static_cast<char>(value);xsputn(&ch,1);}return value;
    }
    int sync() override {if(actor)actor->capture();return 0;}
};
}
int main(int argc,char** argv) {
    const bool local=argc<2 || std::string(argv[1])=="--local";
    const std::string address=local?"127.0.0.1:3040":argv[1];
    const int count=argc>2?std::stoi(argv[2]):4;
    const bool forceRules=argc>3 && std::string(argv[3])=="1";
    const int measurementMs=argc>4?std::stoi(argv[4]):1200;
    assert(count>=4 && count<32);
    assert(measurementMs>=1200 && measurementMs<=60000);
    std::unique_ptr<RelayServer> server;
    if(local) {server=std::make_unique<RelayServer>(1640);server->bind(3040);}
    std::recursive_mutex outputMutex;Output output;
    const auto room="Start-observer-"+std::to_string(getpid());
    int reports[2],release[2];assert(pipe(reports)==0 && pipe(release)==0);
    assert(fcntl(reports[0],F_SETFL,O_NONBLOCK)==0);
    // Fork before creating transport workers. Every client owns its terminal,
    // input, WebSocket worker and complete App initialization/game loop.
    std::vector<pid_t> children;
    for(int index=0;index<=count;++index) {
        const auto pid=fork();assert(pid>=0);
        if(pid>0) {children.push_back(pid);continue;}
        close(reports[0]);close(release[1]);
        auto p=std::make_unique<Player>(index,count,outputMutex,measurementMs);actor=p.get();
        const auto wait=[&](auto done) {
            const auto end=std::chrono::steady_clock::now()+15s;
            while(!done() && std::chrono::steady_clock::now()<end) {
                poll_connection(p->network);std::this_thread::sleep_for(1ms);
            }
            assert(done());
        };
        const auto request=[&](RoomOperation operation,int value=-1,const std::string& name="") {
            request_room(p->network,operation,name,false,false,value);wait([&]{return !p->network.pendingRoomRequest;});
        };
        p->user.serverIp=address;initialize_network_services(p->context,address);
        wait([&]{return p->network.connection==ConnectionState::Connected;});
        if(index==0)request(RoomOperation::Create,-1,room);
        else {
            const auto joinBy=std::chrono::steady_clock::now()+15s;
            do {request(RoomOperation::Join,-1,room);assert(std::chrono::steady_clock::now()<joinBy);}
            while(p->network.room.name!=room);
        }
        if(p->spectator)request(RoomOperation::SetRole,1);
        if(index==0) {
            wait([&]{return p->network.roomMembers.size()==std::size_t(count+1) &&
                std::any_of(p->network.roomMembers.begin(),p->network.roomMembers.end(),[](const auto& m){return m.role==PlayerRole::Spectator;});});
            set_room_policy(p->network,{forceRules});wait([&]{return !p->network.pendingRoomRequest;});
            request(RoomOperation::Prepare);
        }
        wait([&]{return p->network.matchPhase==RoomMatchPhase::Preparing;});
        App::initialize_game(p->context);
        p->input.keyStateProvider=[&p](int code){return p->key(code);};
        try {App::run(p->context);}catch(const Finished&){}
        assert(p->network.matchPhase==RoomMatchPhase::Playing);
        if(!p->spectator)assert(p->session->player.gameplay.scoring.placedBlocks>=1);
        std::ostringstream evidence;
        if(p->spectator) {
            const double seconds=std::chrono::duration<double>(p->measuredUntil-p->started).count();
            const double fps=p->paintedFrames/seconds;
            evidence<<count<<" fighters + observer, forceRules="<<forceRules<<": spectator paint="<<fps
                <<" fps / "<<seconds<<"s; focus changes="<<p->remoteChanges
                <<"; max paint gap="<<p->maxPaintGap*1000<<"ms; max focus update gap="<<p->maxRemoteGap*1000<<"ms\n";
            assert(p->session->roomScene.spectator && !p->input.gameplay_input_active());
            assert(p->session->player.gameplay.scoring.placedBlocks==0 && p->movementQueued);
            assert(fps>=90 && fps<=140 && p->remoteChanges>=3);
        }
        evidence<<p->user.username<<" countdown 3/2/1: "<<p->digits[0]<<p->digits[1]<<p->digits[2]
            <<"; placed="<<p->session->player.gameplay.scoring.placedBlocks<<'\n';
        const auto log=evidence.str();assert(write(STDERR_FILENO,log.data(),log.size())==static_cast<ssize_t>(log.size()));
        assert(p->digits[0] && p->digits[1] && p->digits[2]);
        assert(write(reports[1],"1",1)==1);
        char released;assert(read(release[0],&released,1)==0);
        disconnect_online(p->network);
        std::_Exit(0);
    }
    close(reports[1]);close(release[0]);
    bool passed=true,released=false;int completed=0;
    const auto finishedBy=std::chrono::steady_clock::now()+25s+std::chrono::milliseconds(measurementMs);
    while(!children.empty()) {
        char reported[32];const auto received=read(reports[0],reported,sizeof reported);
        if(received>0)completed+=received;
        if(!released && (completed==count+1 || !passed)) {close(release[1]);released=true;}
        for(auto it=children.begin();it!=children.end();) {
            if(std::chrono::steady_clock::now()>finishedBy)kill(*it,SIGKILL);
            int status=0;
            if(waitpid(*it,&status,WNOHANG)==*it) {passed=passed && WIFEXITED(status) && WEXITSTATUS(status)==0;it=children.erase(it);}
            else ++it;
        }
        if(server)server->poll(1ms);else std::this_thread::sleep_for(1ms);
    }
    close(reports[0]);if(!released)close(release[1]);
    assert(passed);
}
