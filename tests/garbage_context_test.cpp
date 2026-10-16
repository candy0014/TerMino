#include "tetr/engine/garbage_service.h"
#include "tetr/engine/live_piece_periodic_events.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/replay_application.h"
#include "tetr/engine/placement_board_adapter.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/locked_line_clear_phase.h"
#include "tetr/runtime/game_context.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/rules/quick_play.h"
#include "tetr/runtime/playfield.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/piece.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "support/terminal_screen.h"
#include "../src/engine/placement_side_effects.h"
#include "../src/engine/garbage_rendering.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>

namespace {
struct Capture {
    std::ostringstream stream;
    std::streambuf* old=std::cout.rdbuf(stream.rdbuf());
    ~Capture() { std::cout.rdbuf(old); }
    std::string str() const { return stream.str(); }
};

void paint_field(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    auto& board=player.gameplay.playfield;
    for (int row=-Runtime::kDisplayHiddenRows;row<board.visibleHeight;++row)
        for (int column=0;column<board.width;++column) {
            const auto value=board.field.cell(row,column);
            const bool visible=Render::board_cell_visible(context,player,row,column,value);
            const bool dimmed=value.occupied() && !visible && context.config.mode.replayMode;
            Render::move_to(context,player,row,column);
            Render::set_board_cell_color(value,context.config.display.obscuredColors,dimmed);
            if (value.is_timed_number()) Render::draw_number(context.config.display.squareStyle,value.timed_countdown());
            else Render::draw_square(context.config.display.squareStyle,
                visible || dimmed ? Render::SquareFill::Filled : Render::SquareFill::Empty);
        }
}

void same_field(const Test::TerminalScreen& actual,const Test::TerminalScreen& expected,
    const Runtime::PlayerSessionState& player) {
    const auto& board=player.gameplay.playfield;
    for (int row=1;row<=board.visibleHeight+Runtime::kDisplayHiddenRows;++row)
        for (int column=player.presentation.margin+1;column<=player.presentation.margin+board.width*2;++column) {
            const auto a=actual.at(row,column), e=expected.at(row,column);
            if (!(a.glyph==e.glyph && a.background==e.background))
                std::cerr<<"Garbage display differs at "<<row<<","<<column<<": "<<a.glyph<<" / "<<e.glyph<<"\n";
            assert(a.glyph==e.glyph && a.background==e.background);
            if (a.glyph!=" " && !a.glyph.empty()) assert(a.foreground==e.foreground);
        }
}

void replay_visibility_changes_update_existing_cells() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    Replay::ReplaySessionState replay;
    context.attach_replay(replay);
    auto& player=session.player;
    auto& board=player.gameplay.playfield;
    board.width=10; board.visibleHeight=board.activeHeight=20; board.garbageHeight=6;
    board.field.initialize(10,20);
    player.presentation.margin=30;
    context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    context.config.mode.replayMode=true;
    context.config.field.invisible=true;
    for (int row=14;row<20;++row) board.field.set_cell(row,0,Runtime::BoardCell::garbage());
    board.field.set_cell(12,0,Runtime::BoardCell::piece(0));
    for (int style:{0,1}) {
        context.config.display.squareStyle=style;
        context.config.quickPlay.invisible=Runtime::ModifierMode::Enabled;
        player.presentation.invisibleFlash=false;
        Test::TerminalScreen actual;
        { Capture c; paint_field(context,player); actual.feed(c.str()); }
        for (int transition=0;transition<4;++transition) {
            const auto before=Engine::GarbageRendering::capture_field(context,player,board.field);
            if (transition<2) player.presentation.invisibleFlash=transition==0;
            else if (transition==2) context.config.quickPlay.invisible=Runtime::ModifierMode::Reverse;
            else board.garbageHeight=3;
            { Capture c;
              Engine::GarbageRendering::render_field_changes(context,player,board.field,before);
              actual.feed(c.str()); }
            Test::TerminalScreen expected;
            { Capture c; paint_field(context,player); expected.feed(c.str()); }
            same_field(actual,expected,player);
            const auto pieceColor=transition==0 ? Test::TerminalScreen::Color{{54,235,168}}
                : Test::TerminalScreen::Color{{18,78,56}};
            assert(actual.at(21,31).foreground==pieceColor);
            if (transition==2) assert((actual.at(26,31).foreground==Test::TerminalScreen::Color{{26,26,26}}));
            if (transition==3) assert((actual.at(26,31).foreground==Test::TerminalScreen::Color{{78,78,78}}));
        }
        board.garbageHeight=6;
    }
    context.config.quickPlay.invisible=Runtime::ModifierMode::Enabled;
    context.config.field.periodicInvisible=true;
    // A restored/seeked separate game clock can cross either flash edge
    // without advancing a normal frame or changing any board cell.
    replay.separateGameTime=true;
    replay.gameClockPaused=true;
    for (double elapsed:{5.2,6.0,10.5,11.0}) {
        replay.lastGameTimestamp=Runtime::Duration::from_seconds(elapsed);
        Engine::synchronize_field_visibility(context,player);
        assert(player.presentation.invisibleFlash==(elapsed==5.2 || elapsed==10.5));
    }
}

void reverse_invisible_clear_repaints_equal_garbage() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    Replay::ReplaySessionState replay;
    context.attach_replay(replay);
    auto& player=session.player;
    auto& board=player.gameplay.playfield;
    board.width=context.config.field.width=4;
    board.visibleHeight=board.activeHeight=context.config.field.height=8;
    board.field.initialize(4,8); board.garbageHeight=6;
    context.config.mode.mode=Runtime::GameMode::Zen;
    context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    context.config.quickPlay.invisible=Runtime::ModifierMode::Reverse;
    context.config.field.lineClearDelay={};
    player.presentation.margin=30;
    player.presentation.blockMaxHeight=player.presentation.blockMaxWidth=4;
    for (int row=2;row<8;++row) for (int col=1;col<4;++col)
        board.field.set_cell(row,col,Runtime::BoardCell::garbage());
    auto piece=Piece::get_definition(Piece::Type::from_index(0));
    const auto& cells=piece.cells(Runtime::Rotation{1});
    int maxRow=-100,minColumn=100;
    for (const auto& offset:cells) { maxRow=std::max(maxRow,offset[0]); minColumn=std::min(minColumn,offset[1]); }
    Engine::MovementState movement;
    movement.pose={{7-maxRow,-minColumn},Runtime::Rotation{1}};
    assert(piece.can_place(movement.pose,board.field));
    Test::TerminalScreen actual;
    { Capture c; paint_field(context,player); actual.feed(c.str()); }
    assert(actual.at(15,33).glyph==" "); // Deep garbage was invisible.
    { Capture c;
      const auto result=Engine::finalize_locked_piece(context,board.field,piece,player,movement,1,
          [] { return Rules::PlayResult::ContinueFrame; });
      assert(result==Rules::PlayResult::Locked);
      actual.feed(c.str()); }
    assert(board.garbageHeight==2);
    assert(actual.at(15,33).glyph=="█"); // Equal garbage now belongs to the shallow three rows.
    Test::TerminalScreen expected;
    { Capture c; paint_field(context,player); expected.feed(c.str()); }
    same_field(actual,expected,player);
}

void multipliers_preserve_fractions_and_replay() {
    Capture capture;
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    Replay::ReplaySessionState replay;
    context.attach_replay(replay);
    context.config.garbage.mode=Runtime::GarbageMode::Survival;
    context.config.garbage.hitMultiplier=.1;
    auto& player=session.player;
    auto& field=player.gameplay.playfield.field;
    field.initialize(10,20);
    player.gameplay.playfield.visibleHeight=-100;
    for (int i=0;i<10;++i) {
        Engine::GarbageService::add_buffer(context,player,field,1);
        assert(Rules::pending_garbage(player.garbage.buffer)==(i+1)/10);
    }
    assert(player.garbage.incomingFraction<1e-9);
    context.config.garbage.hitMultiplier=.123456789;
    Engine::GarbageService::add_buffer(context,player,field,1);
    assert(std::abs(player.garbage.incomingFraction-.123456789)<1e-12);

    Runtime::SessionState playbackSession;
    Runtime::RuntimeServices playbackServices;
    Runtime::GameContext playback(playbackSession,playbackServices);
    Replay::ReplaySessionState playbackReplay;
    playback.attach_replay(playbackReplay);
    playback.config=context.config;
    playback.config.mode.replayMode=true;
    auto& replayPlayer=playbackSession.player;
    auto& replayField=replayPlayer.gameplay.playfield.field;
    replayField.initialize(10,20);
    replayPlayer.gameplay.playfield.visibleHeight=-100;
    auto piece=Piece::get_definition(Piece::Type::from_index(0));
    auto operations=Replay::decode_operation_stream(replay.data,0);
    while (!operations.empty()) {
        Engine::apply_replay_gameplay_event(playback,operations.front(),replayField,replayPlayer,piece);
        operations.pop();
    }
    assert(Rules::pending_garbage(replayPlayer.garbage.buffer)==1);
    assert(replayPlayer.garbage.incomingFraction==player.garbage.incomingFraction);

    // Replaying cancellation must reproduce unused fractional attack as well.
    Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
    context.config.garbage.hitMultiplier=1;
    context.config.garbage.cancelMultiplier=2;
    replay.data.clear();
    Engine::GarbageService::add_buffer(context,player,field,3);
    assert(Engine::GarbageService::offset_buffer(context,player,field,4,1)==2);
    assert(player.garbage.cancellation.outgoingFraction==.5);
    Engine::GarbageService::add_buffer(context,player,field,1);
    assert(Engine::GarbageService::offset_buffer(context,player,field,1,1)==1);
    playback.config=context.config; playback.config.mode.replayMode=true;
    Engine::GarbageService::initialize(replayPlayer,playback.config.garbage.holePattern);
    operations=Replay::decode_operation_stream(replay.data,0);
    while (!operations.empty()) {
        Engine::apply_replay_gameplay_event(playback,operations.front(),replayField,replayPlayer,piece);
        operations.pop();
    }
    assert(replayPlayer.garbage.buffer.empty());
    assert(replayPlayer.garbage.cancellation.outgoingFraction==player.garbage.cancellation.outgoingFraction);
    assert(replayPlayer.garbage.cancellation.cancellationFraction==player.garbage.cancellation.cancellationFraction);

    Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
    context.config.garbage.hitMultiplier=.5;
    context.config.garbage.legacyIncomingRounding=true;
    for (int i=0;i<2;++i) Engine::GarbageService::add_buffer(context,player,field,1);
    assert(player.garbage.buffer.empty() && player.garbage.incomingFraction==0);
    context.config.garbage.legacyIncomingRounding=false;
    context.config.garbage.hitMultiplier=0;
    Engine::GarbageService::add_buffer(context,player,field,100);
    assert(player.garbage.buffer.empty());

    // Zero cancellation must still send attacks while pending garbage remains.
    context.config.garbage.mode=Runtime::GarbageMode::Backfire;
    context.config.garbage.hitMultiplier=1;
    context.config.garbage.cancelMultiplier=0;
    Engine::GarbageService::add_buffer(context,player,field,5);
    assert(Engine::GarbageService::offset_buffer(context,player,field,4,1)==4);
    assert(Rules::pending_garbage(player.garbage.buffer)==9);
    Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
    assert(player.garbage.incomingFraction==0 && player.garbage.cancellation.cancellationFraction==0 &&
        player.garbage.cancellation.outgoingFraction==0);

    // Scale Quick Play's resolved packet while retaining its RNG sequence.
    context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    context.config.garbage.hitMultiplier=.5;
    player.attackRoundingRandom.seed(17);
    auto random=player.attackRoundingRandom;
    int received=0;
    for (int i=0;i<20;++i) {
        const auto& qp=context.session.quickPlay;
        const double raw=Rules::qp_receive_attack(4,qp.altitude,qp.altitude,qp.floor,
            context.config.quickPlay,qp.progress,Runtime::elapsed_game_time(context.session,context.now()).seconds());
        const double roll=static_cast<double>(random())/(static_cast<double>(std::mt19937::max())+1);
        received+=static_cast<int>(std::floor(raw))+(roll<raw-std::floor(raw));
        Engine::GarbageService::add_buffer(context,player,field,4);
        assert(Rules::pending_garbage(player.garbage.buffer)==received/2);
    }
    assert(player.attackRoundingRandom==random);

    // Unclear rises directly instead of queueing; reception still accumulates.
    Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
    field.clear();
    player.gameplay.playfield.width=10;
    context.config.garbage.mode=Runtime::GarbageMode::Unclear;
    context.config.garbage.hitMultiplier=.5;
    context.config.garbage.attackMultiplier=.5;
    assert(Engine::capture_placement_application_config(context,0).garbageMultiple==1);
    Engine::PlacementPreOffsetEffects effects; effects.directGarbageLines=1;
    replay.data.clear();
    Engine::apply_pre_offset_placement_garbage(context,field,player,effects);
    assert(player.gameplay.playfield.garbageHeight==0 && player.garbage.incomingFraction==.5);
    Engine::apply_pre_offset_placement_garbage(context,field,player,effects);
    assert(player.gameplay.playfield.garbageHeight==1 && player.garbage.incomingFraction==0);
    assert(player.garbage.buffer.empty());
    playback.config=context.config; playback.config.mode.replayMode=true;
    Engine::GarbageService::initialize(replayPlayer,playback.config.garbage.holePattern);
    replayPlayer.gameplay.playfield.width=10;
    replayField.clear();
    operations=Replay::decode_operation_stream(replay.data,0);
    while (!operations.empty()) {
        Engine::apply_replay_gameplay_event(playback,operations.front(),replayField,replayPlayer,piece);
        operations.pop();
    }
    assert(replayPlayer.gameplay.playfield.garbageHeight==1 && replayPlayer.garbage.incomingFraction==0);
}

void garbage_updates_match_complete_field_rendering() {
    for (int style:{0,1}) for (bool obscured:{false,true})
        for (int visibility=0;visibility<4;++visibility) for (int kind=0;kind<3;++kind) {
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(session,services);
        Replay::ReplaySessionState replay;
        context.attach_replay(replay);
        context.config.display.squareStyle=style;
        context.config.display.obscuredColors=obscured;
        context.config.field.invisible=visibility==1 || visibility==3;
        context.config.garbage.mode=visibility==2 ? Runtime::GarbageMode::QuickPlay : Runtime::GarbageMode::CheeseTimer;
        context.config.quickPlay.invisible=visibility==2 ? Runtime::ModifierMode::Reverse : Runtime::ModifierMode::Disabled;
        auto& player=session.player;
        auto& board=player.gameplay.playfield;
        board.width=10; board.visibleHeight=board.activeHeight=20;
        board.field.initialize(10,20);
        player.presentation.margin=30;
        player.presentation.invisibleFlash=visibility==3;
        player.garbage.random.seed(123);
        Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
        for (int row=-8;row<20;++row) for (int col=0;col<10;++col)
            if (col!=(row+10)%10) board.field.set_cell(row,col,row<8
                ? Runtime::BoardCell::piece((row+8)%7) : Runtime::BoardCell::garbage());
        board.garbageHeight=12;
        board.field.set_cell(19,0,Runtime::BoardCell::timed_solid(7));
        board.field.set_cell(19,1,Runtime::BoardCell::timed_release());
        board.field.set_cell(19,2,Runtime::BoardCell::timed_solid());
        Test::TerminalScreen screen;
        { Capture capture; paint_field(context,player); screen.feed(capture.str()); }
        for (int rise:{1,3}) {
            {
                Capture capture;
                if (kind==0) Engine::GarbageService::add_garbage(context,player,board.field,rise);
                else Engine::GarbageService::add_solid_garbage(context,player,board.field,rise,
                    kind==1 ? Runtime::BoardCell::solid_garbage() : Runtime::BoardCell::timed_solid(9));
                screen.feed(capture.str());
                assert(capture.str().find("HOLD")==std::string::npos && capture.str().find("NEXT")==std::string::npos);
            }
            Test::TerminalScreen expected;
            { Capture capture; paint_field(context,player); expected.feed(capture.str()); }
            same_field(screen,expected,player);
        }
    }
}

void timed_rise_updates_piece_and_ghost_together() {
    std::size_t optimizedBytes=0, fullBytes=0;
    for (const auto type:Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus))
        for (int style:{0,1}) for (bool obscured:{false,true}) for (bool ghost:{false,true})
            for (bool landed:{false,true}) for (bool pieceVisible:{false,true}) {
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(session,services);
        Replay::ReplaySessionState replay;
        context.attach_replay(replay);
        context.config.garbage.mode=Runtime::GarbageMode::CheeseTimer;
        context.config.garbage.generationInterval=Runtime::Duration::from_seconds(.1);
        context.config.display.squareStyle=style;
        context.config.display.obscuredColors=obscured;
        context.config.field.ghost=ghost;
        auto& player=session.player;
        auto& board=player.gameplay.playfield;
        board.width=10; board.activeHeight=board.visibleHeight=20;
        board.field.initialize(10,20);
        player.presentation.margin=30;
        player.garbage.random.seed(123);
        Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
        auto piece=Piece::get_definition(type);
        Engine::MovementState movement;
        movement.pose={{0,3},Runtime::Rotation{}};
        if (landed) while (piece.can_place(movement.pose.translated(1,0),board.field)) ++movement.pose.position.row;
        const auto originalPose=movement.pose;
        const auto originalLock=movement.lockTimer;
        Test::TerminalScreen screen;
        {
            Capture capture;
            paint_field(context,player);
            if (pieceVisible) Render::draw_piece(context,piece,player,movement.pose,board.field);
            screen.feed(capture.str());
        }
        player.timing.timedGarbage=context.now()-Runtime::Duration::from_seconds(1);
        player.timing.framePoll=context.real_now();
        {
            Capture capture;
            assert(!Engine::process_live_piece_periodic_events(context,board.field,piece,player,movement,false,
                [] { return Rules::PlayResult::ContinueFrame; },pieceVisible));
            screen.feed(capture.str());
            optimizedBytes+=capture.str().size();
        }
        Test::TerminalScreen expected;
        {
            Capture capture;
            paint_field(context,player);
            Render::draw_piece(context,piece,player,movement.pose,board.field);
            expected.feed(capture.str());
            fullBytes+=capture.str().size();
        }
        same_field(screen,expected,player);
        assert(board.garbageHeight==1 && piece.can_place(movement.pose,board.field));
        assert(movement.pose.position.row==originalPose.position.row-(landed?1:0));
        assert(movement.lockTimer==originalLock); // Rendering does not reset lock delay.
        assert(replay.data.find("{put0,")<replay.data.find("{add_garbage,"));
        assert(replay.data.find("{add_garbage,")<replay.data.find("{put1,"));
    }
    assert(optimizedBytes*8<fullBytes);
    std::cerr<<"Timed garbage incremental / full rendering: "<<optimizedBytes<<" / "<<fullBytes<<" bytes\n";
}

void buffered_garbage_deadline_and_replay() {
    const auto setup=[](Runtime::GameContext& context, Runtime::GarbageMode mode) {
        context.config.simulationPps=1;
        context.config.garbage.mode=mode;
        context.config.garbage.incomingDelay=Runtime::Duration::from_seconds(.4);
        context.session.gameStartTime=Runtime::TimePoint{};
        auto& player=context.session.player;
        auto& board=player.gameplay.playfield;
        board.width=10; board.activeHeight=board.visibleHeight=20;
        board.field.initialize(10,20);
        player.presentation.margin=30;
        player.garbage.random.seed(123);
        Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
    };
    for (auto mode:{Runtime::GarbageMode::Backfire,Runtime::GarbageMode::Survival,
                   Runtime::GarbageMode::QuickPlay,Runtime::GarbageMode::Multiplayer}) {
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(session,services);
        Replay::ReplaySessionState replay;
        context.attach_replay(replay);
        setup(context,mode);
        auto& player=session.player;
        auto& board=player.gameplay.playfield;
        auto piece=Piece::get_definition(Piece::Type::from_index(0));
        Engine::MovementState movement;
        movement.pose={{0,3},Runtime::Rotation{}};
        while (piece.can_place(movement.pose.translated(1,0),board.field)) ++movement.pose.position.row;
        Capture capture;
        Engine::GarbageService::add_buffer(context,player,board.field,3,false,3);
        assert(player.presentation.garbageBufferDisplay[1]==1);
        session.simulationTime=Runtime::TimePoint::from_seconds(.2);
        Engine::GarbageService::update_buffer(context,player);
        assert(player.presentation.garbageBufferDisplay[1]==2);
        session.simulationTime=Runtime::TimePoint::from_seconds(.4);
        Engine::GarbageService::update_buffer(context,player);
        assert(player.presentation.garbageBufferDisplay[1]==3);
        player.timing.framePoll=context.real_now();
        bool risen=false;
        assert(!Engine::process_live_piece_periodic_events(context,board.field,piece,player,movement,false,
            [] {return Rules::PlayResult::ContinueFrame;},true,&risen));
        assert(!risen && board.garbageHeight==0 && Rules::pending_garbage(player.garbage.buffer)==3);

        // Disabling lock gating must raise the expired packet during movement.
        Test::TerminalScreen screen;
        paint_field(context,player);
        Render::draw_piece(context,piece,player,movement.pose,board.field);
        screen.feed(capture.str());
        capture.stream.str("");
        context.config.garbage.entryMode=Runtime::GarbageEntryMode::Immediate;
        assert(!Engine::process_live_piece_periodic_events(context,board.field,piece,player,movement,false,
            [] {return Rules::PlayResult::ContinueFrame;},true,&risen));
        assert(risen && board.garbageHeight==3 && player.garbage.buffer.empty());
        assert(piece.can_place(movement.pose,board.field));
        screen.feed(capture.str());
        capture.stream.str("");
        paint_field(context,player);
        Render::draw_piece(context,piece,player,movement.pose,board.field);
        Test::TerminalScreen expected;
        expected.feed(capture.str());
        same_field(screen,expected,player);
        assert(capture.str().find("\x1b[38;2;")!=std::string::npos);

        Runtime::SessionState replaySession;
        Runtime::RuntimeServices replayServices;
        Runtime::GameContext playback(replaySession,replayServices);
        Replay::ReplaySessionState playbackState;
        playback.attach_replay(playbackState);
        setup(playback,mode);
        playback.config.mode.replayMode=true;
        playback.config.garbage.entryMode=Runtime::GarbageEntryMode::Immediate;
        auto operations=Replay::decode_operation_stream(replay.data,0);
        while (!operations.empty()) {
            const auto operation=operations.front(); operations.pop();
            playbackState.playback.elapsed=operation.timestamp;
            Engine::apply_replay_gameplay_event(playback,operation,replaySession.player.gameplay.playfield.field,
                replaySession.player,piece);
        }
        assert(replaySession.player.garbage.buffer.empty());
        assert(replaySession.player.gameplay.playfield.garbageHeight==3);
        for (int row=-Runtime::kHiddenRowCapacity;row<20;++row)
            for (int col=0;col<10;++col)
                assert(replaySession.player.gameplay.playfield.field.cell(row,col)==board.field.cell(row,col));
    }

    // The default still cancels on clears and waits for a non-clearing lock.
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    Replay::ReplaySessionState replay;
    context.attach_replay(replay);
    setup(context,Runtime::GarbageMode::Backfire);
    auto& player=session.player;
    auto& board=player.gameplay.playfield;
    Capture capture;
    Engine::GarbageService::add_buffer(context,player,board.field,3);
    session.simulationTime=Runtime::TimePoint::from_seconds(.4);
    Engine::run_frame(context,board.field,player,false);
    assert(board.garbageHeight==0 && Rules::pending_garbage(player.garbage.buffer)==3);
    Engine::GarbageService::offset_buffer(context,player,board.field,1,1);
    assert(board.garbageHeight==0 && Rules::pending_garbage(player.garbage.buffer)==2);
    Engine::GarbageService::offset_buffer(context,player,board.field,0,0);
    assert(board.garbageHeight==2 && player.garbage.buffer.empty());

    // Idle/entry-delay frames also apply immediate garbage without a new lock.
    Engine::GarbageService::add_buffer(context,player,board.field,2);
    context.config.garbage.entryMode=Runtime::GarbageEntryMode::Immediate;
    session.simulationTime=Runtime::TimePoint::from_seconds(.799);
    Engine::run_frame(context,board.field,player,false);
    assert(board.garbageHeight==2);
    session.simulationTime=Runtime::TimePoint::from_seconds(.8);
    Engine::run_frame(context,board.field,player,false);
    assert(board.garbageHeight==4 && player.garbage.buffer.empty());
    context.config.timeMultiplier=.25;
    Engine::GarbageService::add_buffer(context,player,board.field,1,true);
    assert(player.presentation.garbageBufferDisplay[1]==3);
    Engine::run_frame(context,board.field,player,false);
    assert(board.garbageHeight==5 && player.garbage.buffer.empty());

    for (auto entry:{Runtime::GarbageEntryMode::AfterNonClear,Runtime::GarbageEntryMode::AfterLock,
                    Runtime::GarbageEntryMode::Immediate}) for (bool cleared:{false,true}) {
        Runtime::SessionState lockSession;
        Runtime::RuntimeServices lockServices;
        Runtime::GameContext locked(lockSession,lockServices);
        Replay::ReplaySessionState lockReplay;
        locked.attach_replay(lockReplay);
        setup(locked,Runtime::GarbageMode::Backfire);
        locked.config.garbage.entryMode=entry;
        locked.config.garbage.cap=2;
        auto& p=lockSession.player;
        auto& f=p.gameplay.playfield.field;
        Engine::GarbageService::add_buffer(locked,p,f,3);
        lockSession.simulationTime=Runtime::TimePoint::from_seconds(.4);
        Engine::GarbageService::offset_buffer(locked,p,f,cleared?1:0,cleared?1:0);
        if (entry==Runtime::GarbageEntryMode::AfterLock || cleared)
            assert(p.gameplay.playfield.garbageHeight==0);
        auto piece=Piece::get_definition(Piece::Type::from_index(0));
        Rules::PlacementResult placement;
        placement.clear.clearedLines=cleared?1:0;
        assert(Engine::complete_locked_piece_side_effects(locked,f,piece,p,placement,{})==Rules::PlayResult::Locked);
        const int expectedRise=entry==Runtime::GarbageEntryMode::AfterNonClear ? (cleared?0:2) :
            2;
        assert(p.gameplay.playfield.garbageHeight==expectedRise);
        assert(Rules::pending_garbage(p.garbage.buffer)==3-(cleared?1:0)-expectedRise);

        Runtime::SessionState replaySession;
        Runtime::RuntimeServices replayServices;
        Runtime::GameContext playback(replaySession,replayServices);
        Replay::ReplaySessionState playbackState;
        playback.attach_replay(playbackState);
        setup(playback,Runtime::GarbageMode::Backfire);
        playback.config=locked.config;
        playback.config.mode.replayMode=true;
        auto operations=Replay::decode_operation_stream(lockReplay.data,0);
        while (!operations.empty()) {
            const auto operation=operations.front(); operations.pop();
            playbackState.playback.elapsed=operation.timestamp;
            Engine::apply_replay_gameplay_event(playback,operation,replaySession.player.gameplay.playfield.field,
                replaySession.player,piece);
        }
        assert(replaySession.player.gameplay.playfield.garbageHeight==expectedRise);
        assert(Rules::pending_garbage(replaySession.player.garbage.buffer)==Rules::pending_garbage(p.garbage.buffer));
        for (int row=-Runtime::kHiddenRowCapacity;row<20;++row)
            for (int col=0;col<10;++col)
                assert(replaySession.player.gameplay.playfield.field.cell(row,col)==f.cell(row,col));
    }
}

void immediate_garbage_during_line_clear() {
    for (auto mode:{Runtime::GarbageEntryMode::AfterNonClear,Runtime::GarbageEntryMode::AfterLock,
                   Runtime::GarbageEntryMode::Immediate}) {
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(session,services);
        Replay::ReplaySessionState replay;
        context.attach_replay(replay);
        context.config.simulationPps=1;
        context.config.field.width=4;
        context.config.field.height=6;
        context.config.field.lineClearDelay=Runtime::Duration::from_seconds(.2);
        context.config.garbage.mode=Runtime::GarbageMode::Backfire;
        context.config.garbage.entryMode=mode;
        context.config.garbage.incomingDelay=Runtime::Duration::from_seconds(.1);
        session.gameStartTime=Runtime::TimePoint{};
        auto& player=session.player;
        auto& board=player.gameplay.playfield;
        board.width=4; board.activeHeight=board.visibleHeight=6;
        board.field.initialize(4,6);
        player.presentation.margin=30;
        Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
        for (int col=0;col<4;++col) board.field.set_cell(5,col,Runtime::BoardCell::piece(0));
        board.field.set_cell(4,0,Runtime::BoardCell::piece(1));
        Rules::PlacementResult placement;
        placement.clear.clearedLines=1;
        placement.clear.clearedRows={5};
        auto piece=Piece::get_definition(Piece::Type::from_index(0));
        Capture capture;
        Engine::GarbageService::add_buffer(context,player,board.field,1);
        int frames=0;
        const auto pump=[&] {
            session.simulationTime=Runtime::TimePoint::from_seconds(++frames*.06);
            player.timing.framePoll=context.real_now();
            return Rules::PlayResult::ContinueFrame;
        };
        assert(Engine::run_locked_line_clear_phase(context,board.field,piece,player,placement,pump)==Rules::PlayResult::ContinueFrame);
        assert(frames>=2);
        Engine::GarbageService::update_buffer(context,player);
        if (mode==Runtime::GarbageEntryMode::Immediate) {
            assert(player.garbage.buffer.empty() && board.garbageHeight==1);
            assert(board.field.cell(4,0)==Runtime::BoardCell::piece(1));
            int garbage=0;
            for (int col=0;col<4;++col) garbage+=board.field.cell(5,col).is_normal_garbage();
            assert(garbage==3); // The new row must survive the interrupted clear.
        } else {
            assert(Rules::pending_garbage(player.garbage.buffer)==1 && board.garbageHeight==0);
            assert(board.field.cell(5,0)==Runtime::BoardCell::piece(1));
        }

        // Seeking can rebuild all due clear/garbage events in a single frame.
        Runtime::SessionState playbackSession;
        Runtime::RuntimeServices playbackServices;
        Runtime::GameContext playback(playbackSession,playbackServices);
        Replay::ReplaySessionState playbackState;
        playback.attach_replay(playbackState);
        playback.config=context.config;
        playback.config.mode.replayMode=true;
        auto& rp=playbackSession.player;
        auto& rb=rp.gameplay.playfield;
        rb.width=4; rb.activeHeight=rb.visibleHeight=6;
        rb.field.initialize(4,6);
        rp.presentation.margin=30;
        Engine::GarbageService::initialize(rp,playback.config.garbage.holePattern);
        for (int col=0;col<4;++col) rb.field.set_cell(5,col,Runtime::BoardCell::piece(0));
        rb.field.set_cell(4,0,Runtime::BoardCell::piece(1));
        playbackState.operations=Replay::decode_operation_stream(replay.data,0);
        playbackState.playback.elapsed=Runtime::Duration::from_seconds(1);
        playbackState.playback.rebuilding=true;
        assert(Engine::run_locked_line_clear_phase(playback,rb.field,piece,rp,placement,
            [] {return Rules::PlayResult::ContinueFrame;})==Rules::PlayResult::ContinueFrame);
        assert(playbackState.operations.empty());
        assert(Rules::pending_garbage(rp.garbage.buffer)==Rules::pending_garbage(player.garbage.buffer));
        assert(rp.presentation.garbageBufferDisplay==player.presentation.garbageBufferDisplay);
        for (int row=-Runtime::kHiddenRowCapacity;row<6;++row)
            for (int col=0;col<4;++col) assert(rb.field.cell(row,col)==board.field.cell(row,col));
    }
}

void zero_duration_buffer_display() {
    const std::string red="\x1b[38;2;233;22;0m";
    const auto setup=[](Runtime::GameContext& context,Runtime::GarbageEntryMode entry,int cap,double clearDelay) {
        context.config.simulationPps=1;
        context.config.garbage.mode=Runtime::GarbageMode::Backfire;
        context.config.garbage.entryMode=entry;
        context.config.garbage.incomingDelay={};
        context.config.garbage.cap=cap;
        context.config.field.lineClearDelay=Runtime::Duration::from_seconds(clearDelay);
        context.session.gameStartTime=Runtime::TimePoint{};
        auto& player=context.session.player;
        auto& board=player.gameplay.playfield;
        board.width=10; board.activeHeight=board.visibleHeight=20;
        board.field.initialize(10,20);
        player.presentation.margin=30;
        player.garbage.random.seed(123);
        Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
    };
    for (auto entry:{Runtime::GarbageEntryMode::AfterNonClear,Runtime::GarbageEntryMode::AfterLock,
                    Runtime::GarbageEntryMode::Immediate}) for (int cap:{0,2,8}) for (double clearDelay:{0.,.05}) {
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(session,services);
        Replay::ReplaySessionState replay;
        context.attach_replay(replay);
        setup(context,entry,cap,clearDelay);
        auto& player=session.player;
        auto& board=player.gameplay.playfield;
        Capture capture;
        if (entry==Runtime::GarbageEntryMode::Immediate)
            Engine::GarbageService::add_buffer(context,player,board.field,3);
        else assert(Engine::GarbageService::offset_buffer(context,player,board.field,3,1)==3);
        const bool deferred=cap>0 && (entry==Runtime::GarbageEntryMode::Immediate ||
            (entry==Runtime::GarbageEntryMode::AfterLock && clearDelay==0));
        assert(player.presentation.garbageBufferDisplayDeferred==deferred);
        assert((capture.str().find(red)==std::string::npos)==deferred);
        Engine::GarbageService::update_buffer(context,player);
        assert((capture.str().find(red)==std::string::npos)==deferred);

        auto piece=Piece::get_definition(Piece::Type::from_index(0));
        Rules::PlacementResult placement;
        placement.clear.clearedLines=1;
        if (entry==Runtime::GarbageEntryMode::Immediate)
            Engine::run_frame(context,board.field,player,false);
        else assert(Engine::complete_locked_piece_side_effects(context,board.field,piece,player,placement,{})==Rules::PlayResult::Locked);
        const int risen=entry==Runtime::GarbageEntryMode::AfterNonClear ? 0 : std::min(cap,3);
        assert(board.garbageHeight==risen && Rules::pending_garbage(player.garbage.buffer)==3-risen);
        assert(!player.presentation.garbageBufferDisplayDeferred);
        if (deferred && risen==3) assert(capture.str().find(red)==std::string::npos);
        assert(player.presentation.garbageBufferDisplay[1]==(risen<3 ? 3 : 0));

        Runtime::SessionState playbackSession;
        Runtime::RuntimeServices playbackServices;
        Runtime::GameContext playback(playbackSession,playbackServices);
        Replay::ReplaySessionState playbackState;
        playback.attach_replay(playbackState);
        setup(playback,entry,cap,clearDelay);
        playback.config.mode.replayMode=true;
        auto events=Replay::decode_operation_stream(replay.data,0);
        capture.stream.str("");
        bool recordedDefer=false;
        while (!events.empty()) {
            const auto event=events.front(); events.pop();
            playbackState.playback.elapsed=event.timestamp;
            if (event.kind==Replay::OperationKind::AddBuffer)
                recordedDefer=event.payload_as<Replay::AddBufferPayload>().deferDisplay;
            Engine::apply_replay_gameplay_event(playback,event,playbackSession.player.gameplay.playfield.field,
                playbackSession.player,piece);
        }
        assert(recordedDefer==(deferred && entry==Runtime::GarbageEntryMode::AfterLock));
        assert(playbackSession.player.presentation.garbageBufferDisplay==player.presentation.garbageBufferDisplay);
        assert(!playbackSession.player.presentation.garbageBufferDisplayDeferred);
        assert(playbackSession.player.gameplay.playfield.garbageHeight==risen);
        if (deferred && risen==3) assert(capture.str().find(red)==std::string::npos);
    }

    // A previously visible bar must be cleared when zero-time garbage settles.
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    Replay::ReplaySessionState replay;
    context.attach_replay(replay);
    setup(context,Runtime::GarbageEntryMode::AfterNonClear,8,0);
    auto& player=session.player;
    auto& field=player.gameplay.playfield.field;
    Capture capture;
    Engine::GarbageService::add_buffer(context,player,field,2);
    assert(player.presentation.garbageBufferDisplay[1]==3);
    context.config.garbage.entryMode=Runtime::GarbageEntryMode::Immediate;
    capture.stream.str("");
    Engine::GarbageService::add_buffer(context,player,field,1);
    Engine::run_frame(context,field,player,false);
    assert(player.garbage.buffer.empty() && !player.presentation.garbageBufferDisplayDeferred);
    for (int state:player.presentation.garbageBufferDisplay) assert(state==0);
    assert(capture.str().find(red)==std::string::npos);
}
}

int main() {
    replay_visibility_changes_update_existing_cells();
    reverse_invisible_clear_repaints_equal_garbage();
    multipliers_preserve_fractions_and_replay();
    garbage_updates_match_complete_field_rendering();
    timed_rise_updates_piece_and_ghost_together();
    buffered_garbage_deadline_and_replay();
    immediate_garbage_during_line_clear();
    zero_duration_buffer_display();
    Runtime::SessionState baselineSession;
    Runtime::RuntimeServices baselineServices;
    Runtime::GameContext baseline(baselineSession, baselineServices);
    Replay::ReplaySessionState baselineReplay;
    baseline.attach_replay(baselineReplay);
    const std::string baselineReplayData = Replay::session(baseline).data;
    const double baselineMultiplier = baseline.config.garbage.attackMultiplier;
    const Runtime::GarbageMode baselineModel = baseline.config.garbage.mode;

    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session, services);
    Replay::ReplaySessionState contextReplay;
    context.attach_replay(contextReplay);
    context.config.mode.replayMode = false;
    context.config.garbage.mode = Runtime::GarbageMode::Backfire;
    context.config.garbage.attackMultiplier = 2.5;
    context.config.garbage.hitMultiplier = 2.5;
    context.config.garbage.incomingDelay = Runtime::Duration::from_seconds(5.0);
    context.config.timeMultiplier = 1.0;

    Runtime::PlayerSessionState player;
    player.gameplay.playfield.width = 10;
    player.gameplay.playfield.visibleHeight = -100;
    player.gameplay.playfield.activeHeight = 20;
    player.gameplay.scoring.placedBlocks = 20;
    player.garbage.random.seed(1234);
    Engine::GarbageService::initialize(player, context.config.garbage.holePattern);
    Runtime::Playfield field;
    field.initialize(10, 20);

    const Runtime::TimePoint before = context.now();
    Engine::GarbageService::add_buffer(context, player, field, 4, false);
    const Runtime::TimePoint after = context.now();
    assert(player.garbage.buffer.size() == 1);
    assert(player.garbage.buffer.front().line_count() == 10);
    assert(player.garbage.buffer.front().arrival_time() >= before);
    assert(player.garbage.buffer.front().arrival_time() <= after);
    assert(Replay::session(context).data.find("{add_buffer,") == 0);

    const int outgoing = Engine::GarbageService::offset_buffer(context, player, field, 3, 1);
    assert(outgoing == 0);
    assert(player.garbage.buffer.size() == 1);
    assert(player.garbage.buffer.front().line_count() == 7);
    assert(baseline.config.garbage.attackMultiplier == baselineMultiplier);
    assert(baseline.config.garbage.mode == baselineModel);
    assert(Replay::session(baseline).data == baselineReplayData);

    Runtime::SessionState timerSession;
    Runtime::RuntimeServices timerServices;
    Runtime::GameContext timerContext(timerSession, timerServices);
    Replay::ReplaySessionState timerReplay;
    timerContext.attach_replay(timerReplay);
    timerContext.config.garbage.mode = Runtime::GarbageMode::CheeseTimer;
    timerContext.config.garbage.generationInterval = Runtime::Duration::from_seconds(0.1);
    timerContext.config.autoplay.mode = Runtime::AutoplayMode::While1;
    auto& timerPlayer = timerSession.player;
    auto& timerPlayfield = timerPlayer.gameplay.playfield;
    timerPlayfield.width = 10;
    timerPlayfield.visibleHeight = 20;
    timerPlayfield.activeHeight = 20;
    timerPlayfield.field.initialize(10, 20);
    Engine::GarbageService::initialize(timerPlayer, timerContext.config.garbage.holePattern);
    timerPlayer.timing.timedGarbage = timerContext.now() - Runtime::Duration::from_seconds(1.0);
    timerPlayer.timing.frameUpdate = timerContext.now();

    Piece::Definition timerPiece = Piece::get_definition(Piece::Type::from_index(0));
    Engine::MovementState timerMovement;
    const Runtime::TimePoint originalGarbageTime = timerPlayer.timing.timedGarbage;
    const auto timerSignal = Engine::process_live_piece_periodic_events(
        timerContext, timerPlayfield.field, timerPiece, timerPlayer, timerMovement, true,
        [] { return Rules::PlayResult::ContinueFrame; });
    assert(!timerSignal);
    assert(timerPlayfield.garbageHeight == 0);
    assert(timerPlayer.timing.timedGarbage == originalGarbageTime);

    // The chosen piece locks on the original field; the overdue row shifts it
    // only after that lock, as happens across an autoplay Hold continuation.
    const Runtime::BoardCell lockedCell = Runtime::BoardCell::piece(2);
    timerPlayfield.field.set_cell(19, 3, lockedCell);
    Engine::apply_due_cheese_timer_after_autoplay_lock(
        timerContext, timerPlayfield.field, timerPlayer);
    assert(timerPlayfield.garbageHeight == 1);
    assert(timerPlayfield.field.cell(18, 3) == lockedCell);
    assert(timerPlayfield.field.cell(19, 3) != lockedCell);
    assert(timerPlayer.timing.timedGarbage > originalGarbageTime);
    std::cout << "garbage_context_test: OK\n";
}
