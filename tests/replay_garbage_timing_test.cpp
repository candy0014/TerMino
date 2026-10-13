#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/replay_application.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/turn_timeline.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "../src/engine/next_preview_presentation.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
struct Capture {
    std::ostringstream output;
    std::streambuf* previous=std::cout.rdbuf(output.rdbuf());
    ~Capture() { std::cout.rdbuf(previous); }
    std::string take() { auto text=output.str(); output.str({}); return text; }
};
void rule_clock_follows_applied_events() {
    Runtime::SessionState s;Runtime::RuntimeServices services;Runtime::GameContext c(s,services);
    Replay::ReplaySessionState r;c.attach_replay(r);
    c.config.mode.replayMode=true;c.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    s.player.gameplay.playfield.width=10;s.player.gameplay.playfield.visibleHeight=20;
    s.player.gameplay.playfield.field.initialize(10,20);
    Engine::GarbageService::initialize(s.player,c.config.garbage.holePattern);
    r.playback.elapsed=Runtime::Duration::from_seconds(100);
    r.operations=Replay::decode_operation_stream("{move,1,-2,4,0}{move,1.1,-2,4,0}",0);
    s.quickPlay.progress.receiveProtection=18;
    assert(Replay::playback_game_elapsed(c,c.now()).seconds()==0);
    auto piece=Piece::get_definition(Piece::Type::from_index(0));
    const auto op=r.operations.front();r.operations.pop();Replay::mark_operation_consumed(c,op);
    Engine::apply_replay_gameplay_event(c,op,s.player.gameplay.playfield.field,s.player,piece);
    assert(Replay::playback_game_elapsed(c,c.now()).seconds()==1);
    assert(s.quickPlay.progress.receiveProtection==18); // 1 second, not 100 seconds, elapsed.
    assert(r.playback.lastFrameRuleUpdate.seconds()==1);
}
struct Result { int locks=0;Runtime::Playfield field;std::mt19937 random; };
Result audit(const std::string& data,bool backlog) {
    Capture output; Test::TerminalScreen screen;
    Runtime::SessionState s;Runtime::RuntimeServices services;Runtime::GameContext c(s,services);
    Replay::ReplaySessionState r;c.attach_replay(r);Input::Services input;c.attach_input(input);
    Replay::ConfigPrefixResult prefix;assert(Replay::decode_config_prefix(data,c.config,prefix));
    c.config.mode.replayMode=true;r.operations=Replay::decode_operation_stream(data,prefix.operationOffset);
    assert(!r.operations.empty());
    Replay::configure_playback(c,backlog ? 1024 : 1,r.operations.back().timestamp);
    auto& p=s.player;auto& b=p.gameplay.playfield;
    b.width=c.config.field.width;b.visibleHeight=c.config.field.height;
    b.nextCount=c.config.field.nextCount;b.holdEnabled=c.config.field.holdEnabled;
    p.pieces.allowedPieces=Piece::types_for_rotation_system(c.config.rotationSystem);
    p.pieces.random.seed(*prefix.blockSeed);p.garbage.random.seed(*prefix.garbageSeed);
    Engine::initialize_board(c,p);Engine::GarbageService::initialize(p,c.config.garbage.holePattern);
    Engine::initialize_board_timers(c,p);Engine::initialize_quick_play(c,p);
    p.presentation.margin=30; p.presentation.topMargin=Runtime::kDisplayHiddenRows;
    for (int y=-Runtime::kDisplayHiddenRows;y<b.visibleHeight;++y) for (int x=0;x<b.width;++x)
        Render::draw_board_cell(c,p,y,x,b.field.cell(y,x));
    screen.feed(output.take());
    auto piece=Piece::get_definition(Engine::current_piece(p));Engine::MovementState move;int locks=0;
    while (!r.operations.empty()) {
        auto op=r.operations.front();r.operations.pop();
        r.playback.elapsed=backlog ? Runtime::Duration::from_seconds(10000):op.timestamp;
        Replay::mark_operation_consumed(c,op);
        if (op.kind==Replay::OperationKind::StartPlace) {
            Engine::prepare_next_preview(c,p);piece=Piece::get_definition(Engine::current_piece(p));
            p.presentation.activePieceType=piece.type();
        }
        Engine::apply_replay_gameplay_event(c,op,b.field,p,piece);
        if (backlog && !r.operations.empty())
            assert(Replay::playback_progress_elapsed(c,c.now())==op.timestamp);
        if (op.kind==Replay::OperationKind::PutDraw || op.kind==Replay::OperationKind::Move) {
            move.pose=op.payload_as<Replay::PosePayload>().pose;
            if (!piece.can_place(move.pose,b.field)) {
                std::cerr<<"Recorded collision at "<<op.timestamp.seconds()<<"s after "<<locks<<" locks; piece "<<piece.type().index()
                    <<" pose "<<move.pose.position.row<<','<<move.pose.position.col<<','<<move.pose.rotation.index()<<"\n";
                for (const auto& cell:piece.cells(move.pose.rotation)) {
                    const int y=move.pose.position.row+cell[0],x=move.pose.position.col+cell[1];
                    std::cerr<<"  cell "<<y<<','<<x<<" occupied="<<b.field.cell(y,x).occupied()<<" piece="<<b.field.cell(y,x).is_piece()<<" color="<<b.field.cell(y,x).color_id()<<"\n";
                }
            }
            assert(piece.can_place(move.pose,b.field));
        }
        if (op.kind==Replay::OperationKind::Hold) {
            const auto current=Engine::current_piece(p);Engine::consume_current_piece(p);
            if (p.gameplay.hold.piece.valid()) Engine::prepend_piece(p,p.gameplay.hold.piece);
            p.gameplay.hold.piece=current;
            p.presentation.activePose.reset();p.presentation.activePieceType.reset();
            p.presentation.paintedActivePose.reset();p.presentation.paintedActivePieceType.reset();
        }
        if (op.kind==Replay::OperationKind::EndMoveData) {
            const auto payload=op.payload_as<Replay::EndMoveDataPayload>();move.pose=payload.pose;move.lastRotation=payload.lastRotation;
            assert(piece.can_place(move.pose,b.field));
            p.presentation.activePose.reset();p.presentation.activePieceType.reset();
            p.presentation.paintedActivePose.reset();p.presentation.paintedActivePieceType.reset();
            r.playback.elapsed=Runtime::Duration::from_seconds(10000);r.playback.rebuilding=true;
            assert(Engine::finalize_locked_piece(c,b.field,piece,p,move,0,
                [] {return Rules::PlayResult::ContinueFrame;})==Rules::PlayResult::Locked);
            r.playback.rebuilding=false;Engine::consume_current_piece(p);++locks;
        }
        screen.feed(output.take());
        if (op.kind==Replay::OperationKind::PutDraw || op.kind==Replay::OperationKind::Move ||
            op.kind==Replay::OperationKind::AddSolidGarbage || op.kind==Replay::OperationKind::EndMoveData) {
            // Compare accumulated ANSI updates with an independently repainted
            // board at every stable movement, fatigue rise and settlement.
            const auto saved=p.presentation;
            Test::TerminalScreen expected;
            for (int y=-Runtime::kDisplayHiddenRows;y<b.visibleHeight;++y) for (int x=0;x<b.width;++x)
                Render::draw_board_cell(c,p,y,x,b.field.cell(y,x));
            if (p.presentation.paintedActivePose && p.presentation.paintedActivePieceType) {
                auto painted=Piece::get_definition(*p.presentation.paintedActivePieceType);
                Render::draw_piece(c,painted,p,*p.presentation.paintedActivePose,b.field);
            }
            expected.feed(output.take());p.presentation=saved;
            for (int y=1;y<=Runtime::kDisplayHiddenRows+b.visibleHeight;++y)
                for (int x=31;x<=30+2*b.width;++x) {
                    const auto a=screen.at(y,x),e=expected.at(y,x);
                    const bool same=a.glyph==e.glyph && a.background==e.background &&
                        ((a.glyph==" " || a.glyph.empty()) || a.foreground==e.foreground);
                    if (!same) std::cerr<<"Replay display mismatch at "<<op.timestamp.seconds()
                        <<"s after "<<locks<<" locks: terminal "<<y<<','<<x<<"\n";
                    assert(same);
                }
        }
    }
    return {locks,b.field,p.garbage.random};
}
}
int main(int argc,char** argv) {
    Capture output;
    rule_clock_follows_applied_events();
    if (argc>1) {
        std::ifstream file(argv[1]);assert(file.good());
        const std::string data((std::istreambuf_iterator<char>(file)),{});
        const auto ordinary=audit(data,false),backlog=audit(data,true);
        assert(ordinary.locks==backlog.locks && ordinary.locks>0 && ordinary.random==backlog.random);
        for (int y=-Runtime::kHiddenRowCapacity;y<ordinary.field.height();++y)
            for (int x=0;x<ordinary.field.width();++x) assert(ordinary.field.cell(y,x)==backlog.field.cell(y,x));
        std::cerr<<"Recorded replay: "<<ordinary.locks<<" legal locks; exact/backlogged geometry and ANSI rendering agree\n";
    }
}
