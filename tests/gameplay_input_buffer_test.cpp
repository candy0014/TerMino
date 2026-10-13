#include "tetr/engine/player_input.h"
#include "tetr/engine/live_piece_controller.h"
#include "tetr/engine/live_piece_periodic_events.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/input/services.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/session_state.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/dynamic_rules.h"
#include "support/terminal_screen.h"
#include "../src/engine/live_piece_controls.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>
#include <iostream>
#include <sstream>

namespace {
using Clock = std::chrono::steady_clock;
Runtime::TimePoint tp(double t) { return Runtime::TimePoint::from_seconds(t); }
Runtime::Duration dur(double t) { return Runtime::Duration::from_seconds(t); }
Clock::time_point wall(double t) { return Clock::time_point{} + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(t)); }
Runtime::TimePoint gameTime(Clock::time_point t) { return tp(std::chrono::duration<double>(t.time_since_epoch()).count()); }

struct Fixture {
    Input::KeyEventBuffer events;
    Input::Services services;
    Engine::MovementInputState input;
    Engine::MovementState state;
    Engine::PlayerInputConfig config;
    Engine::MovementController movement{state,
        [](Piece::Pose p) { return p.position.col >= 0 && p.position.col < 100 && p.position.row <= 100; },
        [](Piece::Pose& p, Runtime::Rotation rotation, Piece::KickIndex& kick) {
            p.rotation = rotation; kick = Piece::KickIndex{0}; return true;
        }};
    Fixture() {
        std::array<int, Input::kBindableActionCount> keys;
        keys.fill(-1);
        keys[0]=10; keys[1]=11; keys[2]=12; keys[3]=13;
        keys[4]=14; keys[5]=15; keys[7]=16;
        services.bindings=Input::BindingSet::from_key_codes(keys);
        services.keyEventSource=&events;
        services.begin_gameplay_input();
        state.pose={{0,50},Runtime::Rotation{}};
        config.das=dur(0.1); config.arr=dur(0.02); config.lockResetLimit=15;
        config.gravityInterval=Runtime::GravityInterval::periodic(dur(1)); config.softDropFactor=10;
        prepare(0);
    }
    void prepare(double t) {
        input.preparePiece(services.bindings,[&](int k) { return services.gameplay_key_down(k); },tp(t));
    }
    void key(int key, bool down, double t) { events.update(key,down,wall(t)); }
    Engine::PlayerInputResult frame(double t) {
        return Engine::process_buffered_player_input_frame(input,movement,services,{14,15},{10,11},config,tp(t),gameTime);
    }
};

void short_presses_survive_stall() {
    Fixture f;
    f.key(14,true,0.01); f.key(14,true,0.02); // OS autorepeat must not rotate twice.
    f.key(14,false,0.03); f.key(14,true,0.04); f.key(14,false,0.05);
    f.key(10,true,0.06); f.key(10,false,0.07);
    assert(f.frame(0.5).command==Engine::PlayerInputCommand::None);
    assert(f.state.pose.rotation==Runtime::Rotation{2});
    assert(f.state.pose.position.col==49);
    assert(f.input.phase(14)==Engine::KeyPhase::Released);
}

void release_and_direction_order() {
    Fixture f;
    f.key(10,true,0.01); f.key(10,false,0.201);
    f.frame(0.5);
    // Tap at .01, then repeats at .11, .13, .15, .17, .19.
    assert(f.state.pose.position.col==44);
    Fixture reversed;
    reversed.config.das=dur(1);
    reversed.key(11,true,0.01); reversed.key(10,true,0.02);
    reversed.frame(0.03);
    assert(reversed.input.mostRecentActive({10,11})==10); // Physical order, not key-code order.
    assert(reversed.state.pose.position.col==50);
}

void commands_preserve_next_piece_events() {
    Fixture f;
    f.key(14,true,0.01); f.key(14,false,0.02);
    f.key(13,true,0.03); f.key(13,false,0.04);
    f.key(15,true,0.05); f.key(15,false,0.06);
    assert(f.frame(0.5).command==Engine::PlayerInputCommand::HardDrop);
    assert(f.state.pose.rotation==Runtime::Rotation{1});
    assert(f.state.pose.position.row==100);
    assert(f.services.next_gameplay_event());
    f.state.pose={{0,50},Runtime::Rotation{}}; f.prepare(0.5);
    assert(f.input.phase(13)==Engine::KeyPhase::AwaitRelease);
    assert(f.frame(0.6).command==Engine::PlayerInputCommand::None);
    assert(f.state.pose.rotation==Runtime::Rotation{-1});
    assert(!f.services.next_gameplay_event());

    Fixture hold;
    hold.key(16,true,0.01); hold.key(16,false,0.02);
    hold.key(14,true,0.03); hold.key(14,false,0.04);
    assert(hold.frame(0.5).command==Engine::PlayerInputCommand::Hold);
    hold.prepare(0.5);
    assert(hold.frame(0.6).command==Engine::PlayerInputCommand::None);
    assert(hold.state.pose.rotation==Runtime::Rotation{1});
}

void elapsed_repeats_match_fast_frames() {
    Fixture fast, slow;
    fast.key(10,true,0.01); slow.key(10,true,0.01);
    fast.key(12,true,0.02); slow.key(12,true,0.02);
    for (int i=2;i<=55;++i) fast.frame(i*0.01);
    slow.frame(0.55);
    assert(fast.state.pose==slow.state.pose);
    assert(slow.state.pose.position.col==26);
    assert(slow.state.pose.position.row==6);
    assert(std::fabs((fast.input.timestamp(10)-slow.input.timestamp(10)).seconds())<1e-12);
    assert(std::fabs((fast.input.timestamp(12)-slow.input.timestamp(12)).seconds())<1e-12);

    Fixture tiny;
    tiny.config.das=dur(0); tiny.config.arr=dur(1e-100);
    tiny.key(10,true,0.01);
    tiny.frame(1000); // Bounded by available cells, never loop once per overdue tick.
    assert(tiny.state.pose.position.col==0);
}

void independent_local_players_and_lifecycle() {
    Fixture f;
    Input::Services second;
    Input::share_key_state(second,f.services);
    second.bindings=f.services.bindings;
    second.begin_gameplay_input();
    f.key(14,true,0.01); f.key(14,false,0.02);
    f.frame(0.1);
    second.poll_gameplay_events();
    assert(second.next_gameplay_event() && second.next_gameplay_event()->down);
    second.consume_gameplay_event();
    assert(second.next_gameplay_event() && !second.next_gameplay_event()->down);
    f.services.end_gameplay_input();
    f.key(14,true,0.2); f.key(14,false,0.3);
    f.services.begin_gameplay_input(); f.prepare(0.4);
    f.frame(0.5); // Menu/retry events do not leak into a new game.
    assert(f.state.pose.rotation==Runtime::Rotation{1});
}

void buffered_hold_records_the_current_pose() {
    Fixture f;
    Runtime::SessionState session;
    Runtime::RuntimeServices runtime;
    Runtime::GameContext context{session,runtime};
    Replay::ReplaySessionState replay;
    context.attach_input(f.services); context.attach_replay(replay);
    context.config.field.width=10; context.config.field.height=20;
    context.config.field.holdEnabled=true; context.config.field.holdCount=1;
    context.config.handling.das=dur(1); context.config.handling.arr=dur(.02);
    auto& player=session.player;
    auto& field=player.gameplay.playfield.field;
    field.initialize(10,20);
    auto piece=Piece::get_definition(Piece::Type::from_index(0));
    f.state.pose={{0,3},Runtime::Rotation{}};
    const auto drawn=f.state.pose;
    f.key(14,true,.01); f.key(14,false,.02);
    f.key(10,true,.03); f.key(10,false,.04); f.key(16,true,.05);
    Engine::AutoplayCarryState carry;
    std::ostringstream output;
    auto* original=std::cout.rdbuf(output.rdbuf());
    const auto result=Engine::process_live_piece_controls(context,field,piece,player,f.state,f.input,carry,
        false,drawn,{14,15},{10,11},f.movement,
        [&](Piece::Pose p) { return piece.can_place(p,field); },
        [&](Piece::Pose& p, Runtime::Rotation r, Piece::KickIndex& k) { return piece.try_rotate(p,r,field,k); },
        [] { return Rules::PlayResult::ContinueFrame; });
    std::cout.rdbuf(original);
    assert(result.signal==Rules::PlayResult::Hold);
    assert(f.state.pose.rotation==Runtime::Rotation{1} && f.state.pose.position.col==2);
    auto operations=Replay::decode_operation_stream(replay.data,0);
    int moves=0; bool erased=false;
    while (!operations.empty()) {
        const auto op=operations.front(); operations.pop();
        if (op.kind==Replay::OperationKind::Move) ++moves;
        if (op.kind==Replay::OperationKind::PutErase) {
            erased=true;
            assert(op.payload_as<Replay::PosePayload>().pose==f.state.pose);
        }
    }
    assert(moves==2 && erased);
}

void instant_gravity_initial_frame_is_complete() {
    for (const auto type:Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)) {
      for (bool timedRise:{false,true}) {
        Fixture f;
        Runtime::SessionState session;
        Runtime::RuntimeServices runtime;
        Runtime::GameContext context{session,runtime};
        Replay::ReplaySessionState replay;
        context.attach_input(f.services); context.attach_replay(replay);
        context.config.field.width=10; context.config.field.height=20;
        context.config.field.ghost=true;
        context.config.handling.gravityInterval=Runtime::GravityInterval::instant();
        context.config.handling.epld=dur(1);
        if (timedRise) {
            context.config.garbage.mode=Runtime::GarbageMode::CheeseTimer;
            context.config.garbage.generationInterval=dur(.1);
        }
        auto& player=session.player;
        auto& field=player.gameplay.playfield.field;
        player.gameplay.playfield.width=10;
        player.gameplay.playfield.visibleHeight=player.gameplay.playfield.activeHeight=20;
        field.initialize(10,20);
        if (timedRise) {
            Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
            player.timing.timedGarbage=context.now()-dur(1);
        }
        auto piece=Piece::get_definition(type);
        Piece::Pose floor{{0,3},Runtime::Rotation{1}};
        while (piece.can_place(floor.translated(1,0),field)) ++floor.position.row;
        f.state.pose=floor.translated(-1,0); // Overlap with the landing pose.
        f.key(13,true,.01);
        Engine::AutoplayCarryState carry;
        std::ostringstream output, expected;
        auto* original=std::cout.rdbuf(output.rdbuf());
        const auto result=Engine::run_live_piece_movement(context,field,piece,player,f.state,f.input,carry,false,
            [&](Piece::Pose p) { return piece.can_place(p,field); },
            [&](Piece::Pose& p, Runtime::Rotation r, Piece::KickIndex& k) { return piece.try_rotate(p,r,field,k); },
            [] { return Rules::PlayResult::ContinueFrame; });
        std::cout.rdbuf(expected.rdbuf());
        while (!piece.can_place(floor,field)) --floor.position.row;
        for (int row=0;row<field.height();++row) for (int col=0;col<field.width();++col) {
            const auto cell=field.cell(row,col);
            if (!cell.occupied()) continue;
            Render::move_to(context,player,row,col);
            Render::set_board_cell_color(cell,false);
            Render::draw_square(0);
        }
        Render::draw_piece(context,piece,player,floor,field);
        std::cout.rdbuf(original);
        assert(result==Rules::PlayResult::ContinueFrame && f.state.pose==floor);
        Test::TerminalScreen actualScreen, expectedScreen;
        actualScreen.feed(output.str()); expectedScreen.feed(expected.str());
        for (int row=1;row<=40;++row) for (int col=1;col<=65;++col) {
            const auto a=actualScreen.at(row,col), e=expectedScreen.at(row,col);
            if (a.glyph!=e.glyph) {
                std::cerr<<"Instant gravity "<<Piece::metadata(type).name
                    <<", timed rise "<<timedRise<<", cell "<<row<<","<<col<<": ["<<a.glyph<<"] / ["<<e.glyph<<"]\n";
            }
            assert(a.glyph==e.glyph);
            if (a.glyph!=" " && !a.glyph.empty()) assert(a.foreground==e.foreground);
        }
      }
    }
}

void timed_garbage_keeps_queued_keys_and_das() {
    Fixture actual, expected;
    actual.state.pose=expected.state.pose={{0,6},Runtime::Rotation{}};
    actual.key(10,true,.01); expected.key(10,true,.01);
    actual.frame(.12); expected.frame(.12);
    const auto prepared=actual.input.prepared_at();
    const auto repeatTime=actual.input.timestamp(10);
    Runtime::SessionState session;
    Runtime::RuntimeServices runtime;
    Runtime::GameContext context(session,runtime);
    Replay::ReplaySessionState replay;
    context.attach_input(actual.services); context.attach_replay(replay);
    context.config.garbage.mode=Runtime::GarbageMode::CheeseTimer;
    context.config.garbage.generationInterval=dur(.1);
    auto& player=session.player;
    auto& field=player.gameplay.playfield.field;
    player.gameplay.playfield.width=10;
    player.gameplay.playfield.visibleHeight=player.gameplay.playfield.activeHeight=20;
    field.initialize(10,20);
    player.presentation.margin=30;
    Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
    auto piece=Piece::get_definition(Piece::Type::from_index(3));
    player.timing.timedGarbage=context.now()-dur(1);
    player.timing.framePoll=context.real_now();
    class InputDuringOutput : public std::stringbuf {
    public:
        std::function<void()> inject;
        bool injected=false;
        std::streamsize xsputn(const char* text,std::streamsize count) override {
            if (!injected) { injected=true; inject(); }
            return std::stringbuf::xsputn(text,count);
        }
    } output;
    output.inject=[&] {
        for (auto* f:{&actual,&expected}) {
            f->key(14,true,.14); f->key(14,false,.145);
            f->key(14,true,.16); f->key(14,false,.165);
            f->key(10,false,.19);
        }
    };
    auto* original=std::cout.rdbuf(&output);
    assert(!Engine::process_live_piece_periodic_events(context,field,piece,player,actual.state,false,
        [] { return Rules::PlayResult::ContinueFrame; }));
    std::cout.rdbuf(original);
    assert(output.injected && actual.services.gameplay_input_active());
    assert(actual.input.prepared_at()==prepared && actual.input.timestamp(10)==repeatTime);
    assert(actual.frame(.5).command==expected.frame(.5).command);
    assert(actual.state.pose==expected.state.pose && actual.state.pose.rotation==Runtime::Rotation{2});
    assert(actual.input.phase(10)==Engine::KeyPhase::Released && !actual.services.next_gameplay_event());
}

void full_redraw_followed_by_input_clears_old_piece() {
    // Move, hard-drop, hold or fall naturally immediately after a full repaint.
    // Exercise both IN flash boundaries and repaints outside Quick Play.
    for (int repaint:{0,1,2,3,4}) for (int style:{0,1}) for (bool instant:{false,true})
      for (bool ghost:{false,true}) for (int action:{0,1,2}) {
        if (instant && action==2) continue;
      for (const auto type:Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)) {
        Fixture f;
        Runtime::SessionState session;
        Runtime::RuntimeServices runtime;
        Runtime::GameContext context{session,runtime};
        Replay::ReplaySessionState replay;
        context.attach_input(f.services); context.attach_replay(replay);
        context.config.field.width=10; context.config.field.height=20;
        context.config.field.ghost=ghost;
        context.config.field.holdEnabled=true; context.config.field.holdCount=1;
        context.config.display.squareStyle=style;
        context.config.handling.gravityInterval=instant ? Runtime::GravityInterval::instant()
            : Runtime::GravityInterval::disabled();
        context.config.handling.epld=dur(1);
        context.config.handling.das=dur(1);
        auto& player=session.player;
        auto& board=player.gameplay.playfield;
        board.width=10; board.visibleHeight=board.activeHeight=20;
        board.nextCount=0; board.holdEnabled=false;
        board.field.initialize(10,20);
        if (repaint==4) {
            context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
            context.config.garbage.entryMode=Runtime::GarbageEntryMode::Immediate;
            context.config.garbage.incomingDelay=dur(0);
            context.config.garbage.cap=8;
            Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
            for (int col=0;col<10;++col) board.field.set_cell(15,col,Runtime::BoardCell::garbage());
        }
        player.presentation.margin=30;
        player.timing.framePoll=context.real_now()-dur(1);
        if (repaint<2) {
            context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
            context.config.field.invisible=true;
            context.config.quickPlay.invisible=Runtime::ModifierMode::Enabled;
            context.config.simulationPps=2;
            session.gameStartTime=tp(0);
            session.simulationTime=tp(repaint==0 ? 5.1 : 6.1);
            player.timing.frameUpdate=context.now();
            player.presentation.qpInvisibleFlash=repaint==1;
        }
        auto piece=Piece::get_definition(type);
        f.state.pose={{0,3},Runtime::Rotation{}};
        Engine::AutoplayCarryState carry;
        std::ostringstream output,expected;
        auto* original=std::cout.rdbuf(output.rdbuf());
        int frames=0;
        const auto result=Engine::run_live_piece_movement(context,board.field,piece,player,f.state,f.input,carry,false,
            [&](Piece::Pose p) { return piece.can_place(p,board.field); },
            [&](Piece::Pose& p,Runtime::Rotation r,Piece::KickIndex& k) { return piece.try_rotate(p,r,board.field,k); },
            [&] {
                if (++frames>1) {
                    assert(action==2 && frames==2);
                    return Rules::PlayResult::Retry;
                }
                if (repaint<2) Engine::run_frame(context,board.field,player,false,false);
                else {
                    if (repaint==2) {
                        Engine::center_game_boards(context,{100,80});
                        Render::clear_screen();
                    }
                    Render::redraw_player_board(context,player);
                    player.timing.framePoll=context.real_now();
                    if (repaint==4) Engine::GarbageService::add_buffer(context,player,board.field,2);
                }
                const int command=action==0 ? 13 : action==1 ? 16 : -1;
                for (int key:{10,command}) {
                    if (key<0) continue;
                    f.events.update(key,true,Clock::now());
                    f.events.update(key,false,Clock::now());
                }
                if (action==2) {
                    Runtime::set_current_gravity_interval(context,Runtime::GravityInterval::periodic(dur(.000001)));
                    player.timing.framePoll=context.real_now()-dur(1);
                }
                return Rules::PlayResult::ContinueFrame;
            });
        std::cout.rdbuf(expected.rdbuf());
        if (action!=1) {
            player.presentation.activePose=f.state.pose;
            player.presentation.activePieceType=piece.type();
        }
        Render::redraw_player_board(context,player);
        std::cout.rdbuf(original);
        assert(frames==(action==2 ? 2 : 1));
        assert(result==(action==0 ? Rules::PlayResult::ContinueFrame
            : action==1 ? Rules::PlayResult::Hold : Rules::PlayResult::Retry));
        if (action==2) assert(f.state.pose.position.row>0);
        Test::TerminalScreen actualScreen,expectedScreen;
        actualScreen.feed(output.str()); expectedScreen.feed(expected.str());
        for (int row=-Runtime::kDisplayHiddenRows;row<20;++row) for (int col=0;col<20;++col) {
            const int y=player.presentation.topMargin+row+1, x=player.presentation.margin+col+1;
            const auto a=actualScreen.at(y,x),e=expectedScreen.at(y,x);
            if (a.glyph!=e.glyph) std::cerr<<"Repaint "<<repaint<<", instant "<<instant
                <<", ghost "<<ghost<<", action "<<action
                <<", piece "<<Piece::metadata(type).name<<", cell "<<row<<","<<col
                <<": ["<<a.glyph<<"] / ["<<e.glyph<<"]\n";
            assert(a.glyph==e.glyph);
            if (a.glyph!=" " && !a.glyph.empty()) assert(a.foreground==e.foreground);
        }
      }
    }
}
}

int main() {
    short_presses_survive_stall(); release_and_direction_order();
    commands_preserve_next_piece_events(); elapsed_repeats_match_fast_frames();
    independent_local_players_and_lifecycle();
    buffered_hold_records_the_current_pose();
    instant_gravity_initial_frame_is_complete();
    timed_garbage_keeps_queued_keys_and_das();
    full_redraw_followed_by_input_clears_old_piece();
}
