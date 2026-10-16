// Manual capture tool: real bot gameplay through the App and UDP relay.
#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/config/user_config.h"
#include "tetr/input/services.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/transport.h"
#include "tetr/replay/session_state.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "../tests/support/terminal_screen.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <streambuf>
#include <thread>

namespace {
using namespace std::chrono_literals;
struct Finished {};
struct Player {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    UserConfig::Settings user;Network::NetworkServices network;Input::Services input;
    Replay::ReplaySessionState replay;App::ApplicationState app;Legacy::BotStrategy bot;
    Test::TerminalScreen screen;std::filesystem::path directory;int slot=0,count=0;
    std::atomic<int> captured=0;bool early=false,later=false;
    Player(int index,int players,const std::filesystem::path& output,std::recursive_mutex& outputMutex,std::atomic<bool>& stop)
        :directory(output),slot(index),count(players) {
        context.attach_user_settings(user);context.attach_network(network);context.attach_input(input);
        context.attach_replay(replay);context.attach_app(app);context.attach_autoplay(bot);context.attach_output_mutex(outputMutex);
        app.primaryContext=&context;app.headlessLocalMatch=true;
        user.gameMode=4;user.uiLanguage=1;user.username="BOT-"+std::to_string(index+1);
        user.serverIp="127.0.0.1:3019";user.autoplayMode=1;user.autoplayPps=1.8+index*.035;
        user.autoplayWidth=50;user.gravityInterval=-1;user.keyCodes=UserConfig::default_key_codes();
        input.initialized=true;input.keyStateProvider=[&stop](int){if(stop)throw Finished{};return false;};
        Input::refresh_bindings(input,user.keyCodes);
    }
    ~Player(){Network::disconnect_online(network);}
    void capture() {
        if(count>8 ? slot!=1 : slot>0 && !(count==2 && slot==1))return;
        const int pieces=session->player.gameplay.scoring.placedBlocks;
        const bool first=!early && pieces>=4,second=early && !later && pieces>=12;
        if(!first && !second)return;
        if(network.matchPhase!=Network::RoomMatchPhase::Playing || session->roomScene.survivors.size()<std::size_t(count==2?2:3) ||
           screen.position_of(user.username).first<1)return;
        auto visible=Engine::full_multiplayer_slots(context);
        visible.insert(visible.end(),session->roomScene.miniSlots.begin(),session->roomScene.miniSlots.end());
        for(int peer:visible)if(peer!=network.roomPlayerIndex && !network.peerSnapshots.count(peer) && !network.peerMiniBoards.count(peer))return;
        const auto name=std::to_string(count)+"-players-bot-"+std::to_string(slot+1)+(first?"-early":"-later");
        std::ofstream output(directory/(name+".json"));output<<"{\"columns\":120,\"rows\":40,\"cells\":[";
        for(int row=1;row<=40;++row)for(int column=1;column<=120;++column) {
            if(row>1 || column>1)output<<',';const auto cell=screen.at(row,column);
            output<<'['<<column-1<<','<<row-1<<','<<std::quoted(cell.glyph)<<",["<<cell.foreground[0]<<','<<cell.foreground[1]<<','<<cell.foreground[2]
                <<"],["<<cell.background[0]<<','<<cell.background[1]<<','<<cell.background[2]<<"]]";
        }output<<"]}\n";
        std::ofstream evidence(directory/(name+".evidence.txt"));const auto& scoring=session->player.gameplay.scoring;
        evidence<<"Capture: real App::initialize_game + App::run, While1 bot, UDP relay.\n"
            <<"Room players: "<<count<<"; survivors: "<<session->roomScene.survivors.size()<<"; perspective: "<<user.username<<"\n"
            <<"Placed pieces: "<<pieces<<"; lines: "<<scoring.totalLines<<"; sent: "<<scoring.totalOutgoingAttack<<"; received: "<<scoring.totalReceivedAttack<<"\n"
            <<"Remote snapshots: "<<network.peerSnapshots.size()<<"; visible miniatures: "<<session->roomScene.miniSlots.size()<<"\n";
        evidence<<"Miniature caches: "<<network.peerMiniBoards.size()<<"; upstream mini frames: "<<network.miniFramesSent<<"; bytes: "<<network.miniBytesSent<<"\n";
        if(network.boardBudget)evidence<<"Budget: full="<<network.boardBudget->full<<" mini="<<network.boardBudget->columns<<'x'<<network.boardBudget->rows<<" @ "<<network.boardBudget->fps<<" fps\n";
        if(first)early=true;else later=true;++captured;
    }
};
thread_local Player* actor=nullptr;
struct Output : std::streambuf {
    std::streamsize xsputn(const char* text,std::streamsize size) override {if(actor)actor->screen.feed(std::string(text,std::size_t(size)));return size;}
    int overflow(int value) override {if(value!=EOF){const char ch=static_cast<char>(value);xsputn(&ch,1);}return value;}
    int sync() override {if(actor)actor->capture();return 0;}
};
template<class Condition>void wait_for(Condition done,int seconds=20) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
    while(!done()){if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Bot capture timed out");std::this_thread::sleep_for(1ms);}
}
void run(int count,const std::filesystem::path& directory) {
    Network::RelayServer server(6541+count);server.bind(3019);
    std::atomic<bool> relayRunning=true,stop=false;
    std::thread relay([&]{while(relayRunning)server.poll(1ms);});
    struct RelayScope {std::atomic<bool>& running;std::thread& worker;~RelayScope(){running=false;worker.join();}}relayScope{relayRunning,relay};
    std::recursive_mutex outputMutex;std::vector<std::unique_ptr<Player>> players;
    for(int slot=0;slot<count;++slot)players.push_back(std::make_unique<Player>(slot,count,directory,outputMutex,stop));
    const auto pump=[&]{for(auto& p:players)Network::poll_connection(p->network);};
    for(auto& p:players)Network::initialize_network_services(p->context,p->user.serverIp);
    wait_for([&]{pump();for(auto& p:players)if(p->network.connection!=Network::ConnectionState::Connected)return false;return true;});
    const auto request=[&](Player& p,Network::RoomOperation operation,const std::string& name="",int value=-1){
        Network::request_room(p.network,operation,name,false,false,value);wait_for([&]{pump();return !p.network.pendingRoomRequest;});
    };
    request(*players[0],Network::RoomOperation::Create,"Bot-capture-"+std::to_string(count));
    for(int slot=1;slot<count;++slot)request(*players[slot],Network::RoomOperation::Join,players[0]->network.room.name);
    Network::set_room_policy(players[0]->network,{false});wait_for([&]{pump();return !players[0]->network.pendingRoomRequest;});
    request(*players[0],Network::RoomOperation::StartMatch);
    Render::begin_output_suppression();
    std::vector<std::future<void>> initialized;
    for(auto& p:players)initialized.push_back(std::async(std::launch::async,[&p]{App::initialize_game(p->context);}));
    for(auto& done:initialized)done.get();
    for(auto& p:players)request(*p,Network::RoomOperation::SetReady,"",1);
    wait_for([&]{pump();return players[0]->network.matchPhase==Network::RoomMatchPhase::Countdown;});
    for(auto& p:players)Network::send(p->network,"start6");
    wait_for([&]{pump();for(auto& p:players)if(p->network.matchPhase!=Network::RoomMatchPhase::Playing)return false;return true;});
    Render::end_output_suppression();
    std::vector<std::future<void>> running;
    for(auto& p:players)running.push_back(std::async(std::launch::async,[&p]{actor=p.get();try{App::run(p->context);}catch(const Finished&){}actor=nullptr;}));
    try {wait_for([&]{return players[count>8?1:0]->captured>=2;},25);}catch(...){
        stop=true;for(auto& done:running)done.get();
        for(auto& p:players)std::cerr<<p->user.username<<": pieces="<<p->session->player.gameplay.scoring.placedBlocks
            <<" snapshots="<<p->network.peerSnapshots.size()<<" survivors="<<p->session->roomScene.survivors.size()
            <<" phase="<<int(p->network.matchPhase)<<" captured="<<p->captured<<"\n";
        throw;
    }
    stop=true;for(auto& done:running)done.get();
    for(auto& p:players)if(p->session->player.gameplay.scoring.placedBlocks<1)throw std::runtime_error("A bot did not place a piece");
    std::cerr<<"Captured real "<<count<<"-player bot match; all bots placed pieces\n";
}
}
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: multiplayer_bot_gallery OUTPUT_DIRECTORY [PLAYERS]\n";return 2;}
    const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    Output output;auto* previous=std::cout.rdbuf(&output);
    try {if(argc>2)run(std::stoi(argv[2]),directory);else for(int count:{2,3,4,5,6,7,8,16})run(count,directory);}
    catch(const std::exception& error){std::cout.rdbuf(previous);std::cerr<<error.what()<<'\n';return 1;}
    std::cout.rdbuf(previous);return 0;
}
