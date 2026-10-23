#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/config/user_config.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/playback_checkpoint.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/turn_timeline.h"
#include "tetr/runtime/game_context.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace TetrTests::turn_replay_test {
namespace {
using Runtime::Duration;
using Runtime::TimePoint;
using Replay::OperationKind;
struct Fixture {
    Runtime::SessionState state;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{state,services};
    Replay::ReplaySessionState replay;
    Input::Services input;
    Fixture() {
        context.attach_replay(replay);
        context.attach_input(input);
        context.config.mode.mode=Runtime::GameMode::LocalVersus;
        context.config.mode.replayMode=true;
        context.config.simulationPps=2.0;
        context.config.turnPieces=1.0;
        auto& player=state.player;
        player.gameplay.playfield.width=10;
        player.gameplay.playfield.visibleHeight=20;
        player.gameplay.playfield.nextCount=5;
        player.pieces.allowedPieces=Piece::types_for_rotation_system(context.config.rotationSystem);
        Engine::initialize_board(context,player);
        Engine::initialize_board_timers(context,player);
    }
};
Replay::Operation operation(OperationKind kind,double wall,double game) {
    Replay::Operation op{kind,Duration::from_seconds(wall)};
    op.gameTimestamp=Duration::from_seconds(game);
    return op;
}
void clock_and_recording() {
    Fixture f;
    f.context.config.mode.replayMode=false;
    f.state.gameStartTime=TimePoint{};
    f.state.simulationTime=TimePoint::from_seconds(4);
    f.replay.recordingStart=f.context.real_now()-Duration::from_seconds(0.2);
    Replay::add_replay(f.context,OperationKind::StartPlace);
    Replay::add_replay(f.context,OperationKind::EndAll);
    auto decoded=Replay::decode_operation_stream(f.replay.data,0);
    assert(decoded.size()==2);
    assert(decoded.front().kind==OperationKind::StartPlace);
    assert(decoded.front().timestamp.seconds()>=0.2 && decoded.front().timestamp.seconds()<1);
    assert(decoded.front().gameTimestamp->seconds()==4);
    f.context.config.mode.replayMode=true;
    f.replay.separateGameTime=true;
    f.replay.operations=decoded;
    Replay::configure_playback(f.context,1,Duration::from_seconds(10));
    Replay::start_playback(f.context,TimePoint::from_seconds(1));
    Replay::mark_operation_consumed(f.context,decoded.front());
    // The recording test wrote both markers immediately. For the continuous
    // playback clock assertions below, leave only a future event queued.
    f.replay.operations={};
    f.replay.operations.push(operation(OperationKind::EndAll,10,4));
    assert(Replay::playback_game_elapsed(f.context,TimePoint::from_seconds(2)).seconds()==4);
    assert(Replay::playback_elapsed(f.context,TimePoint::from_seconds(2)).seconds()==1);
    Replay::toggle_playback_pause(f.context,TimePoint::from_seconds(2));
    assert(Replay::playback_elapsed(f.context,TimePoint::from_seconds(3)).seconds()==1);
    assert(Replay::playback_game_elapsed(f.context,TimePoint::from_seconds(3)).seconds()==4);
    f.context.config.simulationPps=0;
    Replay::mark_operation_consumed(f.context,operation(OperationKind::PauseGameTime,0.2,4));
    assert(Replay::playback_game_elapsed(f.context,TimePoint::from_seconds(3)).seconds()==4);
    Replay::mark_operation_consumed(f.context,operation(OperationKind::ResumeGameTime,0.7,4));
    assert(Replay::playback_game_elapsed(f.context,TimePoint::from_seconds(3)).seconds()>4);
    auto old=Replay::decode_operation_stream("{start_place,4.0}",0);
    assert(!old.front().gameTimestamp && old.front().timestamp.seconds()==4);
}
void shared_controls_without_graphical_frontend() {
    Fixture a,b;
    Input::FocusState physical;
    a.input.focusSource=&physical;a.input.keyEventSource=&physical.events();
    Input::share_key_state(b.input,a.input);
    Input::refresh_bindings(a.input,UserConfig::default_key_codes());
    Input::refresh_bindings(b.input,UserConfig::default_player_two_key_codes());
    a.input.bindings.add_virtual_actions(0);b.input.bindings.add_virtual_actions(1);
    for(auto* f:{&a,&b}) {
        f->replay.localVersusReplay=true;f->context.config.simulationPps=0;
        Replay::configure_playback(f->context,1,Duration::from_seconds(60));
        f->replay.playback.progressVisible=false;
        Replay::start_playback(f->context,f->context.now());
    }
    Replay::share_playback_clock(a.context,b.context);
    Engine::share_replay_playback_controls(a.context,b.context);
    const auto poll=[&] {
        Engine::update_replay_playback_controls(a.context,a.state.player);
        Engine::update_replay_playback_controls(b.context,b.state.player);
    };
    poll();
    // Even a complete short tap queued between polls acts once for both workers.
    physical.update(Input::virtual_action_key(12),true);
    physical.update(Input::virtual_action_key(12),false);poll();
    assert(Replay::playback_paused(a.context) && Replay::playback_paused(b.context));
    const auto native=std::chrono::steady_clock::now();
    const auto first=Replay::playback_elapsed(a.context,a.services.clock.at(native));
    const auto second=Replay::playback_elapsed(b.context,b.services.clock.at(native+std::chrono::seconds(2)));
    assert(first==second);
    // Player 2 can resume the whole recording, whichever worker polls first.
    physical.update(Input::virtual_action_key(12,1),true);
    Engine::update_replay_playback_controls(b.context,b.state.player);poll();
    assert(!Replay::playback_paused(a.context) && !Replay::playback_paused(b.context));
    physical.update(Input::virtual_action_key(12,1),false);poll();
    Replay::set_playback_speed(b.context,b.context.now(),2);
    assert(Replay::playback_speed(a.context)==2 && Replay::playback_speed(b.context)==2);
    const auto at=std::chrono::steady_clock::now();
    const auto later=at+std::chrono::milliseconds(200);
    assert(std::abs((Replay::playback_elapsed(a.context,a.services.clock.at(later))-
        Replay::playback_elapsed(b.context,b.services.clock.at(later))).seconds())<.00001);
    const auto delta=Replay::playback_elapsed(b.context,b.services.clock.at(later))-
        Replay::playback_elapsed(b.context,b.services.clock.at(at));
    assert(std::abs(delta.seconds()-.4)<.00001);
    // A completed track does not own controls needed by its longer peer.
    Replay::finish_playback(a.context,a.context.now());
    physical.update(Input::virtual_action_key(12),true);
    physical.update(Input::virtual_action_key(12),false);
    Engine::update_replay_playback_controls(b.context,b.state.player);
    assert(Replay::playback_paused(b.context));
}
void alternation(double firstQuota,double secondQuota,const std::vector<int>& expected) {
    Fixture a,b;
    a.context.config.turnPieces=firstQuota;
    b.context.config.turnPieces=secondQuota;
    std::recursive_mutex renderMutex;
    a.context.attach_output_mutex(renderMutex);
    b.context.attach_output_mutex(renderMutex);
    for (auto* f : {&a,&b}) {
        f->replay.separateGameTime=true;
        f->replay.playback.progressVisible=false;
    }
    int counts[2]{};
    // All events are already due. The actual piece loop must still preserve
    // mutually exclusive turns, even at high speed or after a seek backlog.
    for (int side : expected) {
        auto& f=side==1?a:b;
        const double game=counts[side-1]*0.5;
        f.replay.operations.push(operation(OperationKind::StartPlace,0,game));
        f.replay.operations.push(operation(OperationKind::EndPlace,0,game+0.5));
        ++counts[side-1];
    }
    for (auto* f : {&a,&b}) f->replay.operations.push(operation(OperationKind::EndAll,0,3));
    Replay::configure_turn_playback(a.context,b.context);
    std::vector<int> audit;
    std::mutex auditMutex;
    auto run=[&](Fixture& f,int side) {
        Replay::start_playback(f.context,f.context.now());
        const auto outcome=Engine::run_piece_loop(f.context,f.state.player,
            [&](Runtime::Playfield&,Piece::Definition,Runtime::PlayerSessionState& player,bool) {
                auto& queue=f.replay.operations;
                assert(queue.front().kind==OperationKind::StartPlace);
                Replay::mark_operation_consumed(f.context,queue.front()); queue.pop();
                {
                    std::lock_guard<std::mutex> lock(auditMutex);
                    audit.push_back(side);
                }
                ++player.gameplay.scoring.placedBlocks;
                assert(queue.front().kind==OperationKind::EndPlace);
                Replay::mark_operation_consumed(f.context,queue.front()); queue.pop();
                return Rules::PlayResult::Locked;
            });
        assert(outcome==Engine::GameLoopExit::Dead);
        Replay::finish_turn_playback(f.context);
    };
    std::thread second([&]{run(b,2);});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    { std::lock_guard<std::mutex> lock(auditMutex); assert(audit.empty()); }
    std::thread first([&]{run(a,1);});
    first.join(); second.join();
    assert(audit==expected);
}
void legacy_and_checkpoints() {
    Fixture a,b;
    a.replay.operations=Replay::decode_operation_stream(
        "{start_place,0}{end_move_data,0}{end_place,0.5}"
        "{start_place,0.5}{end_move_data,0.5}{end_place,1}{end_all,1}",0);
    b.replay.operations=Replay::decode_operation_stream(
        "{start_place,0}{end_move_data,0}{end_place,0.5}{end_all,0.5}",0);
    Replay::configure_turn_playback(a.context,b.context);
    assert(a.replay.separateGameTime && b.replay.separateGameTime);
    assert(a.replay.operations.front().timestamp<b.replay.operations.front().timestamp);
    assert(a.replay.operations.front().gameTimestamp->seconds()==0);
    auto first=a.replay.operations;
    for(int i=0;i<3;++i) first.pop();
    assert(first.front().timestamp>b.replay.operations.front().timestamp);
    assert(first.front().gameTimestamp->seconds()==0.5);
    assert(a.replay.playback.duration==b.replay.playback.duration);
    Replay::start_playback(a.context,a.context.now());
    a.replay.turnPlayback->completedPieces[0]=15;
    a.replay.lastGameTimestamp=Duration::from_seconds(7.5);
    a.replay.lastConsumedTimestamp=Duration::from_seconds(12);
    Replay::capture_playback_checkpoint(a.context);
    assert(Replay::schedule_playback_checkpoint_restore(a.context,Duration::from_seconds(13)).seconds()==12);
    a.replay.turnPlayback->completedPieces[0]=0;
    a.replay.lastGameTimestamp={};
    assert(Replay::restore_scheduled_playback_checkpoint(a.context));
    assert(a.replay.turnPlayback->completedPieces[0]==15);
    assert(a.replay.lastGameTimestamp.seconds()==7.5);
    Fixture disabled, peer;
    disabled.context.config.turnPieces=0;
    Replay::configure_turn_playback(disabled.context,peer.context);
    assert(!disabled.replay.turnPlayback && !peer.replay.turnPlayback);
}
}
int run(int argc,char** argv) {
    Render::begin_output_suppression();
    clock_and_recording();
    shared_controls_without_graphical_frontend();
    alternation(2.5,1.5,{1,1,1,2,2,1,1,2,1});
    alternation(1e-6,1e-6,{1,2,1,2,1});
    alternation(1e-6,2e-6,{1,2,2,1,2,2,1});
    legacy_and_checkpoints();
    std::cerr<<"turn_replay_test: OK\n";
    return 0;
}
} // namespace TetrTests::turn_replay_test
