#include "tetr/engine/garbage_service.h"
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/autoplay/strategy.h"
#include "tetr/input/services.h"
#include "tetr/engine/live_piece_periodic_events.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/replay_application.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/simulation_time.h"
#include "tetr/engine/entry_delay.h"
#include "tetr/engine/locked_line_clear_phase.h"
#include "tetr/engine/undo.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/rules/garbage_queue.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>

namespace TetrTests::garbage_rise_test {
namespace {
using Runtime::Duration;
struct Capture {
    std::ostringstream stream;
    std::streambuf* old=std::cout.rdbuf(stream.rdbuf());
    ~Capture() { std::cout.rdbuf(old); }
    std::string str() const { return stream.str(); }
};
struct Rig {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Replay::ReplaySessionState replay;
    Runtime::PlayerSessionState& player=session.player;
    Runtime::Playfield& field=player.gameplay.playfield.field;
    Rig() {
        context.attach_replay(replay);
        context.config.simulationPps=1;
        session.gameStartTime=Runtime::TimePoint{};
        context.config.garbage.mode=Runtime::GarbageMode::Survival;
        context.config.garbage.survivalApm=0; // All incoming rows in this rig are explicit.
        context.config.garbage.incomingDelay={};
        context.config.garbage.riseInterval=Duration::from_seconds(.1);
        auto& board=player.gameplay.playfield;
        board.width=10; board.activeHeight=board.visibleHeight=20;
        field.initialize(10,20);
        player.presentation.margin=30;
        player.garbage.random.seed(123);
        Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
    }
    void at(double t) { session.simulationTime=Runtime::TimePoint::from_seconds(t); }
    void receive(int lines) { Engine::GarbageService::add_buffer(context,player,field,lines,false,lines); }
    int start() { return Engine::GarbageService::raise_ready_buffer(context,player,field); }
    int tick() { return Engine::GarbageService::advance_garbage_rise(context,player,field); }
    int pending() const { return Rules::pending_garbage(player.garbage.buffer); }
};
void equal_boards(const Rig& a,const Rig& b) {
    for (int y=-Runtime::kHiddenRowCapacity;y<20;++y) for (int x=0;x<10;++x)
        assert(a.field.cell(y,x)==b.field.cell(y,x));
}
void paint(Rig& r,Piece::Definition& piece,const Engine::MovementState& movement,bool visible=true) {
    for (int y=-Runtime::kDisplayHiddenRows;y<20;++y) for (int x=0;x<10;++x)
        Render::draw_board_cell(r.context,r.player,y,x,r.field.cell(y,x));
    if (visible) Render::draw_piece(r.context,piece,r.player,movement.pose,r.field);
}
void equal_screen(const Test::TerminalScreen& a,const Test::TerminalScreen& b) {
    for (int y=1;y<=20+Runtime::kDisplayHiddenRows;++y) for (int x=31;x<=50;++x) {
        const auto actual=a.at(y,x),expected=b.at(y,x);
        assert(actual.glyph==expected.glyph && actual.background==expected.background);
        if (actual.glyph!=" " && !actual.glyph.empty()) assert(actual.foreground==expected.foreground);
    }
}
void permanent_garbage_keeps_the_visible_piece_and_ghost() {
    for (bool playback:{false,true}) for (bool ghost:{false,true}) for (int style:{0,1})
        for (bool landed:{false,true}) for (const auto type:Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)) {
        if (playback && landed) continue;
        Rig r;r.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
        r.context.config.mode.replayMode=playback;r.context.config.field.ghost=ghost;
        r.context.config.display.squareStyle=style;
        for (int y=12;y<20;++y) for (int x=0;x<10;++x)
            if (x!=2 && x!=3) r.field.set_cell(y,x,Runtime::BoardCell::garbage());
        auto piece=Piece::get_definition(type);Engine::MovementState movement;
        movement.pose={{0,3},Runtime::Rotation{}};
        if (landed) while (piece.can_place(movement.pose.translated(1,0),r.field)) ++movement.pose.position.row;
        const auto originalPose=movement.pose;
        r.player.presentation.activePieceType=type;r.player.presentation.activePose=movement.pose;
        Test::TerminalScreen screen;
        { Capture c;paint(r,piece,movement);screen.feed(c.str()); }
        { Capture c;
          if (playback) Engine::apply_replay_gameplay_event(r.context,
              Replay::Operation{Replay::OperationKind::AddSolidGarbage,Duration::from_seconds(480.101061),
                  Replay::AddSolidGarbagePayload{2,Runtime::BoardCell::solid_garbage()}},r.field,r.player,piece);
          else {
              Engine::GarbageService::add_solid_garbage(r.context,r.player,r.field,2);
              bool changed=false;
              assert(!Engine::apply_immediate_garbage_to_live_piece(r.context,r.field,piece,r.player,movement,true,&changed));
              if (landed) assert(changed);
          }
          screen.feed(c.str()); }
        assert(r.player.gameplay.playfield.activeHeight==18 && r.player.gameplay.playfield.solidGarbageHeight==2);
        assert(piece.can_place(movement.pose,r.field));
        Test::TerminalScreen expected;
        { Capture c;paint(r,piece,movement);expected.feed(c.str()); }
        equal_screen(screen,expected);
        if (!playback && landed) {
            Rig replayed;replayed.context.config=r.context.config;replayed.context.config.mode.replayMode=true;
            for (int y=12;y<20;++y) for (int x=0;x<10;++x)
                if (x!=2 && x!=3) replayed.field.set_cell(y,x,Runtime::BoardCell::garbage());
            replayed.player.presentation.activePieceType=type;replayed.player.presentation.activePose=originalPose;
            Engine::MovementState initial;initial.pose=originalPose;Test::TerminalScreen replayScreen;
            { Capture c;paint(replayed,piece,initial);replayScreen.feed(c.str()); }
            auto events=Replay::decode_operation_stream(r.replay.data,0);
            { Capture c;
              while (!events.empty()) {
                  Engine::apply_replay_gameplay_event(replayed.context,events.front(),replayed.field,replayed.player,piece);
                  events.pop();
              }
              replayScreen.feed(c.str()); }
            equal_boards(r,replayed);
            equal_screen(replayScreen,expected);
        }
        const auto next=movement.pose.translated(0,1);
        if (piece.can_place(next,r.field)) {
            { Capture c;Render::draw_piece_transition(r.context,piece,r.player,movement.pose,next,r.field);screen.feed(c.str()); }
            movement.pose=next;
            Test::TerminalScreen moved;
            { Capture c;paint(r,piece,movement);moved.feed(c.str()); }
            equal_screen(screen,moved);
        }
    }
}
struct RisingDuringSearch final : Autoplay::Strategy {
    bool initialize(Runtime::GameContext&) override { return true; }
    Piece::Type choose_position(Runtime::GameContext& c,Runtime::PlayerSessionState&,
        Runtime::Playfield field,const std::vector<Piece::Type>& next,const std::vector<Piece::Type>&,
        Piece::Pose start,Autoplay::HoldInput,Piece::Pose& answer,Runtime::RotationRecord& rotation) override {
        answer=start; answer.position.col=1; rotation=Runtime::RotationRecord::none();
        const auto piece=Piece::get_definition(next.front());
        while (piece.can_place(answer.translated(1,0),field)) ++answer.position.row;
        c.session.simulationTime=Runtime::TimePoint::from_seconds(.1);
        return next.front();
    }
    std::string movement_script(Runtime::GameContext&,Runtime::Playfield,Piece::Definition,
        Piece::Pose,Piece::Pose,Runtime::RotationRecord) override { return "LH"; }
};
void autoplay_search_keeps_visible_pose_during_rise() {
    for (const double pps:{0.,20.,4.}) for (const bool continuation:{false,true})
        for (const bool landed:{false,true}) for (const bool ghost:{false,true}) for (const bool painted:{false,true})
        for (const auto type:Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)) {
        Rig r; Input::Services input; RisingDuringSearch strategy;
        r.context.attach_input(input); r.context.attach_autoplay(strategy);
        r.context.config.field.ghost=ghost;
        auto piece=Piece::get_definition(type);
        Engine::prepend_piece(r.player,piece.type());
        { Capture c; r.receive(3); assert(r.start()==1); }
        Engine::MovementState movement; movement.pose={{0,3},Runtime::Rotation{}};
        if (landed) while (piece.can_place(movement.pose.translated(1,0),r.field)) ++movement.pose.position.row;
        const auto visible=movement.pose;
        Test::TerminalScreen screen;
        { Capture c; paint(r,piece,movement,painted); screen.feed(c.str()); }
        Engine::AutoplayCarryState carry;
        if (continuation) {
            carry.selectedPieceType=piece.type();carry.fieldShift=r.player.gameplay.playfield.garbageHeight;
            carry.pose={{0,1},Runtime::Rotation{}};
            while (piece.can_place(carry.pose.translated(1,0),r.field)) ++carry.pose.position.row;
            r.at(.1);
        }
        r.player.timing.autoplay=r.context.real_now()-Duration::from_seconds(1);
        { Capture c;
          const auto result=Engine::run_autoplay_step(r.context,r.field,piece,r.player,movement,carry,
              continuation,pps,false,Runtime::FoolMode::Disabled,Runtime::GravityInterval::disabled(),
              [&](Piece::Pose pose) { return piece.can_place(pose,r.field); },
              [&](Piece::Pose& pose,Runtime::Rotation rotation,Piece::KickIndex& kick) {
                  return piece.try_rotate(pose,rotation,r.field,kick);
              },[] { return Rules::PlayResult::ContinueFrame; });
          assert(result.signal==Rules::PlayResult::ContinueFrame && result.hardDrop && !result.locked);
          // Complete the pose transition that the live-piece owner performs
          // after accepting the landing. The garbage before-image stays exact.
          if(r.player.presentation.paintedActivePose)
              Render::draw_piece_transition(r.context,piece,r.player,*r.player.presentation.paintedActivePose,movement.pose,r.field);
          else Render::draw_piece(r.context,piece,r.player,movement.pose,r.field);
          screen.feed(c.str()); }
        Test::TerminalScreen expected;
        { Capture c; paint(r,piece,movement); expected.feed(c.str()); }
        equal_screen(screen,expected);
        assert(piece.can_place(movement.pose,r.field) && !piece.can_place(movement.pose.translated(1,0),r.field));
        assert(r.pending()==1); // Lock during the original wave, not after it.

        assert(!carry.selectedPieceType.valid());
        auto events=Replay::decode_operation_stream(r.replay.data,0);
        bool checkedErase=false;
        while (!events.empty()) {
            if (events.front().kind==Replay::OperationKind::PutErase) {
                assert(events.front().payload_as<Replay::PosePayload>().pose==visible); checkedErase=true;
            }
            events.pop();
        }
        assert(checkedErase);
    }
}
void qp_garbage_replay_preserves_generation_policy() {
    Capture output;
    for (int floor=1;floor<=10;++floor) for (int protection=0;protection<=18;++protection) for (bool expert:{false,true}) {
        Rig live,playback;
        for (Rig* r:{&live,&playback}) {
            r->context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
            r->context.config.quickPlay.expert=expert ? Runtime::ModifierMode::Reverse : Runtime::ModifierMode::Disabled;
            r->session.gameStartTime=Runtime::TimePoint{};
            for (int x=0;x<10;++x) if (x!=3) r->field.set_cell(19,x,Runtime::BoardCell::garbage());
        }
        live.at(expert ? 700 : 2);live.session.quickPlay.floor=floor;
        live.session.quickPlay.progress.receiveProtection=protection;
        Engine::GarbageService::add_garbage(live.context,live.player,live.field,3,false);
        auto events=Replay::decode_operation_stream(live.replay.data,0);assert(events.size()==1);
        const auto& policy=events.front().payload_as<Replay::AddGarbagePayload>().quickPlayPolicy;
        assert(policy && policy->floor==floor && policy->receiveProtection==protection && policy->elapsedSeconds==(expert ? 700 : 2));
        playback.context.config.mode.replayMode=true;playback.session.quickPlay.floor=11-floor;
        playback.session.quickPlay.progress.receiveProtection=18-protection;
        playback.replay.playback.elapsed=Duration::from_seconds(10000);
        playback.replay.playback.lastFrameRuleUpdate=events.front().timestamp;
        auto piece=Piece::get_definition(Piece::Type{0});
        Engine::apply_replay_gameplay_event(playback.context,events.front(),playback.field,playback.player,piece);
        equal_boards(live,playback);
        assert(live.player.garbage.random==playback.player.garbage.random);
        assert(playback.session.quickPlay.floor==11-floor);
    }
    auto old=Replay::decode_operation_stream("{add_garbage,1,2,0,7}",0);
    assert(!old.front().payload_as<Replay::AddGarbagePayload>().quickPlayPolicy);
}

void replay_erases_painted_pose_in_old_bot_recording() {
    for (const bool ghost:{false,true}) for (const int style:{0,1}) {
        Rig r; r.context.config.mode.replayMode=true;
        r.context.config.field.ghost=ghost; r.context.config.display.squareStyle=style;
        r.field.set_cell(18,1,Runtime::BoardCell::piece(3));
        auto piece=Piece::get_definition(Piece::Type::from_index(1));
        Engine::MovementState movement; movement.pose={{0,3},Runtime::Rotation{}};
        Test::TerminalScreen screen;
        { Capture c; paint(r,piece,movement); screen.feed(c.str()); }
        // The erase pose is the BOT's new landing, which was never painted.
        auto events=Replay::decode_operation_stream("{put0,0,17,1,0}{add_garbage,0,1,0,7}{put1,0,0,3,0}{move,0,15,5,0}",0);
        while (!events.empty()) {
            const auto op=events.front(); events.pop();
            { Capture c; Engine::apply_replay_gameplay_event(r.context,op,r.field,r.player,piece); screen.feed(c.str()); }
            const bool visible=op.kind==Replay::OperationKind::PutDraw || op.kind==Replay::OperationKind::Move;
            if (visible) movement.pose=op.payload_as<Replay::PosePayload>().pose;
            Test::TerminalScreen expected;
            { Capture c; paint(r,piece,movement,visible); expected.feed(c.str()); }
            equal_screen(screen,expected);
        }
        // An extra erase must not erase any settled cells.
        auto erase=Replay::decode_operation_stream("{put0,0,18,1,0}",0).front();
        { Capture c; Engine::apply_replay_gameplay_event(r.context,erase,r.field,r.player,piece);
          Engine::apply_replay_gameplay_event(r.context,erase,r.field,r.player,piece); screen.feed(c.str()); }
        Test::TerminalScreen expected;
        { Capture c; paint(r,piece,movement,false); expected.feed(c.str()); }
        equal_screen(screen,expected);
    }
}
void deadlines_gating_and_cap() {
    Capture capture;
    Rig r;
    r.context.config.garbage.incomingDelay=Duration::from_seconds(.4);
    r.receive(5);
    r.at(.399); assert(r.start()==0);
    r.at(.4);
    Engine::run_frame(r.context,r.field,r.player,false);
    assert(r.player.gameplay.playfield.garbageHeight==0); // Needs a non-clearing lock.
    Engine::GarbageService::offset_buffer(r.context,r.player,r.field,0,0);
    assert(r.player.gameplay.playfield.garbageHeight==1 && r.pending()==4);
    r.at(.499); assert(r.tick()==0);
    r.at(.5); assert(r.tick()==1);
    r.context.config.garbage.incomingDelay=Duration::from_seconds(100);
    r.at(.8); assert(r.tick()==3 && r.pending()==0); // No further lock/delay gating.
    Rig capped;
    capped.context.config.garbage.cap=2;
    capped.receive(5); assert(capped.start()==1);
    capped.at(.1); assert(capped.tick()==1);
    assert(capped.pending()==3 && capped.player.garbage.rising.remainingLines==0);
    assert(capped.start()==0); // The same attack cannot jump the .2 deadline at its cap boundary.
    capped.at(.2); assert(capped.tick()==1);
    capped.at(.3); assert(capped.tick()==1);
    capped.at(.4); assert(capped.start()==1 && capped.pending()==0);
    Rig instant;
    instant.context.config.garbage.riseInterval={};
    instant.context.config.garbage.cap=3;
    instant.receive(5); assert(instant.start()==3 && instant.pending()==2);
    assert(instant.player.garbage.rising.remainingLines==0);
}
void cancellation_and_qp_hesitation() {
    Capture capture;
    Rig r;
    r.receive(4); assert(r.start()==1);
    Engine::GarbageService::offset_buffer(r.context,r.player,r.field,2,1);
    assert(r.pending()==1 && r.player.garbage.rising.remainingLines==1);
    r.at(.1); assert(r.tick()==1 && r.pending()==0);
    r.receive(3); assert(r.start()==1);
    Engine::GarbageService::offset_buffer(r.context,r.player,r.field,2,1);
    r.at(10); assert(r.tick()==0 && r.pending()==0);
    Rig qp;
    qp.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    qp.context.config.garbage.riseInterval=Duration::from_seconds(5.0/60);
    qp.receive(4); assert(qp.start()==0);
    qp.at(5.0/60); assert(qp.tick()==1);
    qp.at(6.0/60);
    Engine::GarbageService::offset_buffer(qp.context,qp.player,qp.field,0,1);
    qp.at(17.0/60); assert(qp.tick()==0);
    qp.at(18.0/60); assert(qp.tick()==1);
    qp.at(23.0/60); assert(qp.tick()==1);
    qp.at(28.0/60); assert(qp.tick()==1 && qp.pending()==0);
}
void rendering_lift_replay_and_responsiveness() {
    std::size_t diffBytes=0,fullBytes=0;
    for (auto type:Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus))
        for (int style:{0,1}) for (bool ghost:{false,true}) for (bool landed:{false,true}) {
        Rig r,playback;
        r.context.config.display.squareStyle=style;
        r.context.config.field.ghost=ghost;
        r.context.config.garbage.entryMode=Runtime::GarbageEntryMode::Immediate;
        r.field.set_cell(19,0,Runtime::BoardCell::piece(0));
        playback.field.set_cell(19,0,Runtime::BoardCell::piece(0));
        auto piece=Piece::get_definition(type);
        Engine::MovementState movement;
        movement.pose={{0,3},Runtime::Rotation{}};
        if (landed) while (piece.can_place(movement.pose.translated(1,0),r.field)) ++movement.pose.position.row;
        movement.lockTimer=Runtime::TimePoint::from_seconds(-1);
        const auto original=movement.pose;
        Test::TerminalScreen screen;
        { Capture c; paint(r,piece,movement); screen.feed(c.str()); }
        { Capture c; r.receive(4); }
        for (double t:{0.,.1,.3}) {
            r.at(t);
            const auto paintRevision=r.player.presentation.activePiecePaintRevision;
            { Capture c;
              bool changed=false;
              assert(!Engine::apply_immediate_garbage_to_live_piece(r.context,r.field,piece,r.player,movement,true,&changed));
              assert(changed);
              screen.feed(c.str()); diffBytes+=c.str().size(); }
            assert(r.player.presentation.activePiecePaintRevision==paintRevision+1); // Catch-up draws only once.
            Test::TerminalScreen expected;
            { Capture c; paint(r,piece,movement); expected.feed(c.str()); fullBytes+=c.str().size(); }
            equal_screen(screen,expected);
            assert(piece.can_place(movement.pose,r.field));
            assert(movement.lockTimer.seconds()==-1);
        }
        assert(movement.pose.position.row==original.position.row-(landed?4:0));
        assert(r.pending()==0);
        // Playback follows explicit per-row events; it must never run its own timer.
        playback.context.config=r.context.config; playback.context.config.mode.replayMode=true;
        auto ops=Replay::decode_operation_stream(r.replay.data,0);
        { Capture c; while (!ops.empty()) {
            const auto op=ops.front(); ops.pop(); playback.replay.playback.elapsed=op.timestamp;
            Engine::apply_replay_gameplay_event(playback.context,op,playback.field,playback.player,piece);
        } }
        equal_boards(r,playback); assert(playback.pending()==0);
        assert(playback.player.garbage.random==r.player.garbage.random);
        assert(playback.tick()==0);
    }
    assert(diffBytes*5<fullBytes);
    std::cerr<<"Rolling garbage diff/full rendering: "<<diffBytes<<"/"<<fullBytes<<" bytes\n";
    Capture capture;
    Rig r;
    r.receive(8); assert(r.start()==1);
    auto piece=Piece::get_definition(Piece::Type::from_index(1));
    Engine::MovementState movement; movement.pose={{0,3},Runtime::Rotation{}};
    Engine::MovementController controller(movement,[&](Piece::Pose p) { return piece.can_place(p,r.field); },
        [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) {return false;});
    for (int frame=1;frame<6;++frame) {
        r.at(frame*.01);
        bool changed=true;
        assert(!Engine::apply_immediate_garbage_to_live_piece(r.context,r.field,piece,r.player,movement,false,&changed));
        assert(!changed && r.pending()==7);
        assert(controller.moveHorizontal(frame%2 ? 1:-1,r.context.now(),15).changed);
    }
}
void shape_clock_and_restore() {
    Capture capture;
    Rig batch,rolling;
    batch.context.config.garbage.riseInterval={};
    for (Rig* r:{&batch,&rolling}) { r->receive(3); r->receive(2); r->start(); }
    Engine::begin_undo_history(rolling.context);
    rolling.at(.4); assert(rolling.tick()==4);
    equal_boards(batch,rolling);
    assert(batch.player.garbage.random==rolling.player.garbage.random);
    assert(Engine::restore_previous_undo_snapshot(rolling.context));
    assert(rolling.player.garbage.rising.remainingLines==4 && rolling.pending()==4);
    rolling.at(.1); assert(rolling.tick()==1);
    rolling.at(.4); assert(rolling.tick()==3); equal_boards(batch,rolling);
    Rig fast;
    fast.context.config.timeMultiplier=2;
    fast.receive(2); assert(fast.start()==1);
    fast.at(.049); assert(fast.tick()==0); fast.at(.05); assert(fast.tick()==1);
    Rig simulated;
    simulated.context.config.garbage.riseInterval=Duration::from_seconds(.073);
    simulated.receive(4); assert(simulated.start()==1);
    assert(Engine::advance_piece_time(simulated.context,simulated.player)==Rules::PlayResult::ContinueFrame);
    auto ops=Replay::decode_operation_stream(simulated.replay.data,0);
    std::vector<double> times;
    while (!ops.empty()) { if (ops.front().kind==Replay::OperationKind::RaiseBuffer) times.push_back(ops.front().timestamp.seconds()); ops.pop(); }
    assert(times.size()==4);
    for (int i=0;i<4;++i) assert(std::abs(times[i]-i*.073)<1e-9);
}
void continues_during_entry_and_clear() {
    Capture capture;
    for (bool clear:{false,true}) {
        Rig r;
        r.context.config.garbage.riseInterval=Duration::from_seconds(.01);
        r.receive(4); assert(r.start()==1);
        r.context.config.simulationPps=0;
        // Use a real game clock aligned with the already armed wave.
        r.session.clockOffset=r.context.real_now()-Runtime::TimePoint{};
        auto& board=r.player.gameplay.playfield;
        if (!clear) {
            r.context.config.field.entryDelay=Duration::from_seconds(.05);
            Engine::schedule_entry_delay(r.context,r.player);
            assert(Engine::wait_for_entry_delay(r.context,r.player,[&] {
                r.player.timing.framePoll=r.context.real_now(); return Rules::PlayResult::ContinueFrame;
            })==Rules::PlayResult::ContinueFrame);
        } else {
            r.context.config.field.lineClearDelay=Duration::from_seconds(.06);
            for (int x=0;x<10;++x) r.field.set_cell(15,x,Runtime::BoardCell::piece(0));
            Rules::PlacementResult placement;
            placement.clear.clearedRows={15}; placement.clear.clearedLines=1;
            auto piece=Piece::get_definition(Piece::Type::from_index(0));
            assert(Engine::run_locked_line_clear_phase(r.context,r.field,piece,r.player,placement,[&] {
                r.player.timing.framePoll=r.context.real_now(); return Rules::PlayResult::ContinueFrame;
            })==Rules::PlayResult::ContinueFrame);
            // Animation follows the shifted clear row and preserves newly risen garbage.
            for (int x=0;x<10;++x) assert(!r.field.cell(15,x).is_piece());
        }
        assert(r.pending()==0 && board.garbageHeight==4);
    }
}
void hidden_ceiling_and_pause() {
    Capture capture;
    Rig ceiling;
    ceiling.context.config.garbage.entryMode=Runtime::GarbageEntryMode::Immediate;
    auto piece=Piece::get_definition(Piece::Type::from_index(1));
    Engine::MovementState movement;
    const auto& cells=piece.cells(Runtime::Rotation{});
    int minOffset=100;
    for (const auto& cell:cells) minOffset=std::min(minOffset,cell[0]);
    movement.pose={{-Runtime::kHiddenRowCapacity-minOffset,3},Runtime::Rotation{}};
    for (int y=-Runtime::kHiddenRowCapacity;y<20;++y) for (int x=0;x<10;++x)
        ceiling.field.set_cell(y,x,Runtime::BoardCell::garbage());
    for (const auto& cell:cells)
        ceiling.field.set_cell(movement.pose.position.row+cell[0],3+cell[1],Runtime::BoardCell::empty());
    assert(piece.can_place(movement.pose,ceiling.field));
    ceiling.receive(1);
    assert(Engine::apply_immediate_garbage_to_live_piece(ceiling.context,ceiling.field,piece,ceiling.player,movement)
        ==Rules::PlayResult::Dead);
    Rig paused;
    paused.context.config.garbage.riseInterval=Duration::from_seconds(.01);
    paused.receive(2); assert(paused.start()==1);
    paused.context.config.simulationPps=0;
    paused.session.clockOffset=paused.context.real_now()-Runtime::TimePoint{};
    paused.context.pause.started=paused.context.real_now();
    paused.context.pause.paused=true;
    paused.services.clock.sleep_for(Duration::from_seconds(.02));
    assert(paused.tick()==0 && paused.pending()==1);
    paused.context.pause.removedTime+=paused.services.clock.now()-paused.context.pause.started;
    paused.context.pause.paused=false;
    assert(paused.tick()==0); // Paused wall time cannot catch up the roll.
}
void fixtures() {
    std::ostringstream out;
    out<<"{\"rows\":[";
    for (int expert=0;expert<3;++expert) {
        Rig r;
        r.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
        r.context.config.garbage.riseInterval=Duration::from_seconds(5.0/60);
        r.context.config.quickPlay.expert=static_cast<Runtime::ModifierMode>(expert);
        { Capture c; r.receive(4); r.start(); }
        if (expert) out<<',';
        out<<'[';
        for (int frame=1;frame<=27;++frame) {
            r.at(frame/60.0); { Capture c; r.tick(); }
            if (frame>1) out<<','; out<<r.player.gameplay.playfield.garbageHeight;
        }
        out<<']';
    }
    out<<"]}"; std::cout<<out.str()<<'\n';
}
}
int run(int argc,char** argv) {
    if (argc>1 && std::string(argv[1])=="--fixtures") { fixtures(); return 0; }
    deadlines_gating_and_cap();
    permanent_garbage_keeps_the_visible_piece_and_ghost();
    cancellation_and_qp_hesitation();
    rendering_lift_replay_and_responsiveness();
    autoplay_search_keeps_visible_pose_during_rise();
    qp_garbage_replay_preserves_generation_policy();
    replay_erases_painted_pose_in_old_bot_recording();
    shape_clock_and_restore();
    continues_during_entry_and_clear();
    hidden_ceiling_and_pause();
    return 0;
}
} // namespace TetrTests::garbage_rise_test
