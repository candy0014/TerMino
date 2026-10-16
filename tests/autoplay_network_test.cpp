#include "support/autoplay_motion_strategy.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/movement_presentation.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/input/services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/transport.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <thread>

using namespace std::chrono_literals;
using D=Runtime::Duration;
using Clock=std::chrono::steady_clock;
namespace {
using ScriptBot=Test::ScriptMotionBot;
struct Player {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    Network::NetworkServices network;Input::Services input;ScriptBot bot;Replay::ReplaySessionState replay;
    Player() {
        context.attach_network(network);context.attach_input(input);context.attach_autoplay(bot);context.attach_replay(replay);
        UserConfig::Settings settings;settings.gravityInterval=-1;settings.nextCount=1;settings.holdEnabled=0;
        context.config=Runtime::normalize_user_config(settings,Runtime::GameMode::Multiplayer);
        context.config.autoplay.mode=Runtime::AutoplayMode::ChatGPT;context.config.autoplay.pps=2.5;
        context.config.handling.das=D::from_seconds(.04);context.config.handling.arr=D::from_seconds(.02);
        context.config.handling.softDropFixed=true;context.config.handling.softDropFactor=.02;
        input.initialized=true;input.keyStateProvider=[](int){return false;};
        auto& p=session->player;auto& f=p.gameplay.playfield;
        f.width=10;f.visibleHeight=f.activeHeight=20;f.nextCount=1;f.field.initialize(10,20);
        Engine::prepend_piece(p,Piece::Type::from_index(0));p.pieces.nextPreview={Piece::Type::from_index(1)};
    }
    ~Player(){Network::disconnect_online(network);}
};
struct RunningRelay {
    std::unique_ptr<Network::RelayServer> server;std::atomic<bool> running{true};std::thread worker;
    explicit RunningRelay(bool local) {
        if(local) {server=std::make_unique<Network::RelayServer>(9361);server->bind(3038);
            worker=std::thread([&]{while(running)server->poll(1ms);});}
    }
    ~RunningRelay(){running=false;if(worker.joinable())worker.join();}
};
}
int main(int argc,char** argv) {
    RunningRelay relay(argc<2);const std::string url=argc>1?argv[1]:"127.0.0.1:3038";
    Player a,b;
    const auto poll=[&]{Network::poll_connection(a.network);Network::poll_connection(b.network);};
    const auto wait=[&](auto done) {
        const auto until=Clock::now()+15s;
        while(!done() && Clock::now()<until){poll();std::this_thread::sleep_for(1ms);}assert(done());
    };
    const auto request=[&](Player& p,Network::RoomOperation op,const std::string& name="",int value=-1) {
        Network::request_room(p.network,op,name,false,false,value);wait([&]{return !p.network.pendingRoomRequest;});
    };
    Network::initialize_network_services(a.context,url);Network::initialize_network_services(b.context,url);
    wait([&]{return a.network.connection==Network::ConnectionState::Connected && b.network.connection==Network::ConnectionState::Connected;});
    request(a,Network::RoomOperation::Create);request(b,Network::RoomOperation::Join,a.network.room.name);
    Network::set_room_policy(a.network,{false});wait([&]{return !a.network.pendingRoomRequest;});request(a,Network::RoomOperation::StartMatch);
    wait([&]{return a.network.matchEpoch && a.network.matchEpoch==b.network.matchEpoch;});
    Network::LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.nextCount=1;
    setup.nextPreview=a.session->player.pieces.nextPreview;setup.hybridBoardSync=true;
    for(auto* p:{&a,&b}){Network::send(p->network,"start2");Network::send(p->network,Network::encode_lobby_player_setup(setup));}
    wait([&]{return a.network.playerSetups.size()==1 && b.network.playerSetups.size()==1;});
    for(auto* p:{&a,&b})request(*p,Network::RoomOperation::SetReady,"",1);
    for(auto* p:{&a,&b})Network::send(p->network,"start6");
    wait([&]{return a.network.matchPhase==Network::RoomMatchPhase::Playing && b.network.matchPhase==Network::RoomMatchPhase::Playing;});
    for(auto* p:{&a,&b})Network::configure_board_sync(p->context,true,42);
    const auto subscribe=[&](int mode) {
        Network::BoardViews views{2,{},true};if(mode)views.players.push_back({a.network.roomPlayerIndex,mode==2,8,12,mode==2?120:12});
        Network::publish_board_views(b.context,views);
        wait([&]{return a.network.boardBudget && a.network.boardBudget->full==(mode==2) && (mode || !a.network.boardBudget->fps);});
    };
    Network::publish_board_views(a.context,{2,{{b.network.roomPlayerIndex,true,0,0,120}},true});
    Network::send_board_state_frame(b.context,b.session->player);
    std::ostringstream drawing;auto* output=std::cout.rdbuf(drawing.rdbuf());
    struct OutputScope {std::streambuf* old;~OutputScope(){std::cout.rdbuf(old);}} outputScope{output};
    for(int mode:{2,1,0})for(bool handling:{false,true}) {
        subscribe(mode);auto& p=a.session->player;
        a.context.config.autoplay.handlingSimulation=handling;
        auto piece=Piece::get_definition(Engine::current_piece(p));auto movement=Engine::make_spawn_movement_state(10,piece.type());
        const auto start=movement.pose;p.gameplay.scoring.score=0;p.presentation.activePieceType=piece.type();p.presentation.activePose=start;
        Network::send_board_state_frame(a.context,p,Network::ActivePieceState::active(start),{},true);
        const auto firstSequence=a.network.boardSync.sentSequence,motionsBefore=a.network.boardSync.motionsSent;
        const auto snapshotsBefore=a.network.boardSync.snapshotsSent;
        std::set<std::uint64_t> seen;std::vector<Piece::Pose> transmitted;int motionPackets=0;
        const auto pump=[&] {
            poll();Network::flush_piece_motion_frame(a.context);
            for(const auto& wire:b.network.playerInbox)if(wire.slot==a.network.roomPlayerIndex)
                if(const auto packet=Network::decode_frame_packet(wire.data,10,20)) {
                    const auto seq=packet->motion?packet->motion->stamp.sequence:packet->checkpoint?packet->checkpoint->sequence:0;
                    if(seq>firstSequence && seen.insert(seq).second) {
                        if(packet->motion){++motionPackets;transmitted.push_back(packet->motion->result);}
                        else if(packet->opponentState)transmitted.push_back(packet->opponentState->activePose);
                    }
                }
            while(auto packet=Network::try_receive_multiplayer_frame(b.context,false)) {
                if(packet->opponentState)Engine::apply_remote_board_state(b.context,b.session->player,*packet->opponentState,false);
                if(packet->motion)assert(Engine::apply_remote_piece_motion(b.context,b.session->player,*packet->motion,false));
            }
            p.timing.framePoll=a.context.real_now();return Rules::PlayResult::ContinueFrame;
        };
        Engine::AutoplayCarryState carry;Engine::MovementInputState keys;
        p.timing.autoplay=a.context.real_now();const auto began=Clock::now();
        const auto result=handling?Engine::run_handling_autoplay_step(a.context,p.gameplay.playfield.field,piece,p,movement,keys,pump,&carry):
            Engine::run_autoplay_step(a.context,p.gameplay.playfield.field,piece,p,movement,carry,false,2.5,false,Runtime::FoolMode::Disabled,
                Runtime::GravityInterval::disabled(),[&](auto pose){return piece.can_place(pose,p.gameplay.playfield.field);},
                [&](auto& pose,auto turns,auto& kick){return piece.try_rotate(pose,turns,p.gameplay.playfield.field,kick);},pump);
        const auto duration=Clock::now()-began;assert(result.hardDrop && movement.pose!=start);
        const auto currentApplied=[&] {
            const auto timeline=b.network.peerTimelines.find(a.network.roomPlayerIndex);
            return timeline!=b.network.peerTimelines.end() && timeline->second.applied_sequence()>=a.network.boardSync.sentSequence &&
                b.session->player.presentation.activePose==movement.pose;
        };
        const auto until=Clock::now()+3s;
        do {pump();std::this_thread::sleep_for(1ms);}while(mode==2 && !currentApplied() && Clock::now()<until);
        if(mode==2) {
            assert(motionPackets>=2 && transmitted.size()>=3);
            bool horizontal=false,rotated=false;
            for(const auto pose:transmitted)if(pose!=movement.pose) {
                horizontal|=pose.position.col!=start.position.col;rotated|=pose.rotation!=start.rotation;
            }
            assert(horizontal && rotated && currentApplied());
        } else assert(a.network.boardSync.motionsSent==motionsBefore && a.network.boardSync.snapshotsSent==snapshotsBefore);
        std::cerr<<"BOT mode="<<mode<<" handling="<<handling<<" motions="<<motionPackets<<" poses="<<transmitted.size()
            <<" step_ms="<<std::chrono::duration<double,std::milli>(duration).count()<<'\n';
    }
    // Zero-delay animation steps cannot flood the wire. Once the display
    // interval elapses, the last pose must still be delivered.
    subscribe(2);auto& p=a.session->player;const auto piece=Piece::get_definition(Engine::current_piece(p));
    auto pose=Engine::make_spawn_movement_state(10,piece.type()).pose;
    Network::send_board_state_frame(a.context,p,Network::ActivePieceState::active(pose),{},true);
    a.context.pause.paused=true;a.context.pause.started=a.services.clock.now();a.network.boardSync.lastMotion=a.context.real_now();
    const auto before=a.network.boardSync.motionsSent;
    for(int i=0;i<1000;++i){pose.position.col=3+i%3;Network::send_piece_motion_frame(a.context,p,pose,true);}
    assert(a.network.boardSync.motionsSent==before && a.network.boardSync.pendingMotion==pose);
    a.context.pause.started+=D::from_seconds(.004);Network::flush_piece_motion_frame(a.context);
    assert(a.network.boardSync.motionsSent==before && a.network.boardSync.pendingMotion==pose);
    a.context.pause.started+=D::from_seconds(.005);Network::flush_piece_motion_frame(a.context);
    assert(a.network.boardSync.motionsSent==before+1 && a.network.boardSync.pose==pose && !a.network.boardSync.pendingMotion);
    a.context.pause.paused=false;
    Network::configure_board_sync(a.context,false,42);
    Network::send_piece_motion_frame(a.context,p,pose,true);assert(a.network.boardSync.motionsSent==0);
    std::cerr<<"BOT motion transmission, remote reconstruction, hidden/mini subscriptions and burst limit passed\n";
}
