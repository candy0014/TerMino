#include "tetr/app/place.h"
#include "tetr/app/replay_playback.h"
#include "tetr/autoplay/strategy.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/render/board_renderer.h"
#include "support/terminal_screen.h"
#include "tetr/input/services.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/recorder.h"
#include <fstream>
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"

#include <cassert>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
using Runtime::Duration;
using Rules::PlayResult;
Duration seconds(double value) { return Duration::from_seconds(value); }
struct Strategy final:Autoplay::Strategy {
    double computeSeconds=.0;
    std::string script="H";
    Piece::Type selection=Piece::Type::none();
    int decisions=0;
    std::unique_ptr<Autoplay::Strategy> clone_for_search() const override { return std::make_unique<Strategy>(*this); }
    std::any capture_state() const override{return decisions;}
    void restore_state(const std::any& state) override{if(const auto* n=std::any_cast<int>(&state))decisions=*n;}
    bool initialize(Runtime::GameContext&) override { return true; }
    Piece::Type choose_position(Runtime::GameContext& context,Runtime::PlayerSessionState&,Runtime::Playfield field,
        const std::vector<Piece::Type>& sequence,const std::vector<Piece::Type>&,Piece::Pose start,
        Autoplay::HoldInput,Piece::Pose& answer,Runtime::RotationRecord& rotation) override {
        ++decisions;
        context.services.clock.sleep_for(seconds(computeSeconds));
        const auto type=selection.valid() ? selection : sequence.front();
        const auto piece=Piece::get_definition(type);
        answer=start;rotation=Runtime::RotationRecord::none();
        while(piece.can_place(answer.translated(1,0),field)) ++answer.position.row;
        return type;
    }
    std::string movement_script(Runtime::GameContext&,Runtime::Playfield,Piece::Definition,
        Piece::Pose,Piece::Pose,Runtime::RotationRecord) override { return script; }
};
struct Fixture {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};
    Input::Services input;
    Replay::ReplaySessionState replay;
    Strategy strategy;
    App::PieceLoopState loop;
    explicit Fixture(double pps,double softSeconds=0) {
        UserConfig::Settings user;
        user.fieldWidth=4;user.fieldHeight=6;user.nextCount=0;user.holdEnabled=0;
        user.autoplayMode=1;user.autoplayPps=pps;
        user.softDropFactor=softSeconds;user.softDropFixed=1;user.gravityInterval=-1;
        context.config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
        context.attach_input(input);context.attach_replay(replay);context.attach_autoplay(strategy);
        auto& player=session->player;auto& board=player.gameplay.playfield;
        board.width=4;board.visibleHeight=6;board.activeHeight=6;board.field.initialize(4,6);
        player.presentation.margin=20;
        Engine::prepend_piece(player,Piece::Type::from_index(0));
        Engine::initialize_board_timers(context,player);
    }
    PlayResult piece() {
        auto& player=session->player;
        return App::run_piece(context,player.gameplay.playfield.field,
            Piece::get_definition(Engine::current_piece(player)),player,loop);
    }
    auto step(Piece::Definition piece,bool continuation=false,
        const std::function<PlayResult()>& pump=[] {return PlayResult::ContinueFrame;}) {
        auto& player=session->player;auto& field=player.gameplay.playfield.field;
        auto movement=Engine::make_spawn_movement_state(4,piece.type());
        return Engine::run_autoplay_step(context,field,piece,player,movement,loop.autoplay,continuation,
            context.config.autoplay.pps,true,Runtime::FoolMode::Disabled,
            Runtime::current_gravity_interval(context),
            [&](Piece::Pose p) {return piece.can_place(p,field);},
            [&](Piece::Pose& p,Runtime::Rotation r,Piece::KickIndex& k) {return piece.try_rotate(p,r,field,k);},pump);
    }
};
struct RollingHoleBot final:Autoplay::Strategy {
    struct Probe {std::atomic<int> calls{0};std::atomic<bool> thinking{false};};
    std::shared_ptr<Probe> probe=std::make_shared<Probe>();
    std::unique_ptr<Autoplay::Strategy> clone_for_search() const override{return std::make_unique<RollingHoleBot>(*this);}
    bool initialize(Runtime::GameContext&) override{return true;}
    Piece::Type choose_position(Runtime::GameContext& c,Runtime::PlayerSessionState&,Runtime::Playfield field,
        const std::vector<Piece::Type>& next,const std::vector<Piece::Type>&,Piece::Pose start,
        Autoplay::HoldInput,Piece::Pose& answer,Runtime::RotationRecord& record) override {
        ++probe->calls;probe->thinking=true;c.services.clock.sleep_for(seconds(.08));probe->thinking=false;
        const auto piece=Piece::get_definition(next.front());answer=start;answer.rotation=Runtime::Rotation(1);
        answer.position.col=4-piece.cells(answer.rotation).front()[1];record={};
        while(piece.can_place(answer.translated(1,0),field))++answer.position.row;
        return next.front();
    }
    std::string movement_script(Runtime::GameContext&,Runtime::Playfield,Piece::Definition,
        Piece::Pose,Piece::Pose,Runtime::RotationRecord) override{return "XLH";}
};
void locks_and_clears_inside_the_same_rolling_wave() {
    for(bool handling:{false,true})for(double pps:{0.,4.,20.}) {
        double baseline=0;
        for(bool rolling:{false,true}) {
            Fixture f(pps);RollingHoleBot bot;f.context.attach_autoplay(bot);
            auto& p=f.session->player;auto& b=p.gameplay.playfield;
            f.context.config.field.width=b.width=10;f.context.config.field.height=b.visibleHeight=b.activeHeight=20;
            b.field.initialize(10,20);f.context.config.autoplay.handlingSimulation=handling;
            f.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
            f.context.config.garbage.riseInterval=seconds(.03);f.context.config.garbage.cap=20;
            f.context.config.garbage.hitMultiplier=f.context.config.garbage.cancelMultiplier=0;
            f.context.config.field.lineClearDelay={};f.context.config.field.entryDelay={};
            f.session->gameStartTime=f.context.now();
            Engine::initialize_quick_play(f.context,p);
            Engine::GarbageService::initialize(p,f.context.config.garbage.holePattern);
            for(int row=15;row<20;++row)for(int col=0;col<10;++col)
                if(col!=4)b.field.set_cell(row,col,Runtime::BoardCell::garbage());
            b.garbageHeight=5;
            auto hole=std::find(p.garbage.holeOrder.begin(),p.garbage.holeOrder.end(),4);
            std::iter_swap(p.garbage.holeOrder.begin(),hole);p.garbage.previousHoleColumn=4;
            f.session->quickPlay.progress.receiveProtection=18; // Stable, known hole, normal QP policy.
            bool progressedWhileThinking=false;
            f.services.clock.waitInterrupt=[&]{if(bot.probe->thinking && b.garbageHeight>5)progressedWhileThinking=true;};
            if(rolling) {
                Engine::GarbageService::add_buffer(f.context,p,b.field,20,true,20);
                assert(Engine::GarbageService::raise_ready_buffer(f.context,p,b.field)==0);
                assert(p.garbage.rising.remainingLines==20);
            }
            const auto start=f.context.real_now();
            assert(f.piece()==PlayResult::Locked);
            const double elapsed=(f.context.real_now()-start).seconds();
            assert(bot.probe->calls==1 && p.gameplay.scoring.totalLines>=4);
            if(!rolling)baseline=elapsed;
            else {
                assert(progressedWhileThinking && p.garbage.rising.remainingLines>0);
                assert(std::abs(elapsed-baseline)<.045);
                // Check each rise BEFORE the first actual lock. No BOT code
                // may shift the wave's deadlines to make its placement fit.
                auto events=Replay::decode_operation_stream(f.replay.data,0);double previous=-1;int rows=0;
                while(!events.empty()) {
                    const auto op=events.front();events.pop();
                    if(op.kind==Replay::OperationKind::RaiseBuffer) {
                        const double t=op.timestamp.seconds();
                        if(previous>=0)assert(t-previous>=.025 && t-previous<.045);
                        previous=t;++rows;
                    }
                }
                assert(rows>=2);
                std::cerr<<"Rolling QP: handling="<<handling<<" pps="<<pps<<" first lock "<<elapsed
                    <<" / baseline "<<baseline<<", pending="<<p.garbage.rising.remainingLines<<'\n';
            }
        }
    }
}
void full_cycle_cadence(double pps,double compute,double soft,double clear,double entry,int count) {
    Fixture f(pps,soft);
    f.strategy.computeSeconds=compute;
    f.context.config.field.lineClearDelay=seconds(clear);
    f.context.config.field.entryDelay=seconds(entry);
    const auto began=f.context.real_now();
    for(int i=0;i<count;++i) assert(f.piece()==PlayResult::Locked);
    const double elapsed=(f.context.real_now()-began).seconds();
    // A width-four I clears on every turn. The final clear is after the last
    // lock; earlier clears/entries, planning and animation fit inside cadence.
    assert(f.session->player.gameplay.scoring.totalLines==count);
    assert(f.strategy.decisions==count);
    assert(elapsed>=count/pps+clear-.004);
    assert(elapsed<count/pps+clear+.045);
}
void hard_drop_is_instant_when_simulation_is_off() {
    const auto piece=Piece::get_definition(Piece::Type::from_index(0));
    for(double pps:{100.0,0.0}) {
        Fixture f(pps,.006);
        const auto began=f.context.real_now();
        const auto result=f.step(piece);
        assert(result.hardDrop && result.signal==PlayResult::ContinueFrame);
        assert((f.context.real_now()-began).seconds()<.04);
    }
}
void overruns_and_controls() {
    Fixture f(100,.012);f.strategy.computeSeconds=.04;
    const auto start=f.context.real_now();
    for(int i=0;i<3;++i) assert(f.piece()==PlayResult::Locked);
    // Computation bounds achievable PPS; mandatory work never creates catch-up bursts.
    assert((f.context.real_now()-start).seconds()>=.12);
    Fixture interrupted(2,.012);
    interrupted.session->player.timing.framePoll=interrupted.context.real_now()-seconds(1);
    const auto previous=interrupted.session->player.timing.autoplay;
    const auto result=interrupted.step(Piece::get_definition(Piece::Type::from_index(0)),false,
        [] {return PlayResult::Retry;});
    assert(!result.hardDrop && result.signal==PlayResult::Retry);
    assert(interrupted.session->player.timing.autoplay==previous);
}
void handling_cycle_spends_only_remaining_pps_wait() {
    Fixture f(4,.01);f.context.config.autoplay.mode=Runtime::AutoplayMode::ChatGPT;
    f.context.config.autoplay.handlingSimulation=true;f.strategy.computeSeconds=.01;
    f.context.config.field.lineClearDelay=seconds(.015);f.context.config.field.entryDelay=seconds(.02);
    double previous=0,first=0;const auto start=f.context.real_now();
    for(int i=0;i<4;++i) {
        assert(f.piece()==PlayResult::Locked);
        const auto elapsed=(f.context.real_now()-start).seconds();
        if(i) assert(elapsed-previous>=.245 && elapsed-previous<.30);
        else first=elapsed;
        previous=elapsed;
    }
    // A direct hard drop need not reserve 100ms for another path search. The
    // three following slots must still keep their full quarter-second cadence.
    assert(first<.09);
    assert(previous-first>=.735 && previous-first<.82);
}
void hold_uses_one_slot() {
    Fixture f(20,.002);f.strategy.computeSeconds=.008;
    f.strategy.selection=Piece::Type::from_index(3);
    const auto start=f.context.real_now();
    auto held=f.step(Piece::get_definition(Piece::Type::from_index(0)));
    assert(held.signal==PlayResult::Hold && !held.hardDrop);
    assert(f.step(Piece::get_definition(f.strategy.selection),true).hardDrop);
    assert(f.strategy.decisions==1);
    const auto elapsed=(f.context.real_now()-start).seconds();
    assert(elapsed>=.05 && elapsed<.08);
}
void full_speed_quick_play_redraws_and_settles_after_search_rises() {
    for (const double pps:{0.,20.}) for (const bool ghost:{false,true}) for (const bool instant:{false,true}) {
        std::cerr<<"Live render case: pps="<<pps<<" ghost="<<ghost<<"\n";
        Fixture f(pps); auto& p=f.session->player; auto& b=p.gameplay.playfield;
        f.context.config.field.width=b.width=10;
        f.context.config.field.height=b.visibleHeight=b.activeHeight=20;
        f.context.config.field.ghost=ghost; b.field.initialize(10,20);
        if (instant) Runtime::set_current_gravity_interval(f.context,Runtime::GravityInterval::instant());
        f.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
        f.context.config.garbage.riseInterval=seconds(5.0/60.0);
        f.context.config.garbage.incomingDelay={};f.context.config.garbage.hitMultiplier=0;
        Engine::initialize_quick_play(f.context,p);
        Engine::GarbageService::initialize(p,f.context.config.garbage.holePattern);
        f.strategy.computeSeconds=.1;
        b.field.set_cell(19,0,Runtime::BoardCell::piece(3));
        b.field.set_cell(18,1,Runtime::BoardCell::piece(4));
        std::ostringstream output; auto* previous=std::cout.rdbuf(output.rdbuf());
        for (int y=-Runtime::kDisplayHiddenRows;y<20;++y) for (int x=0;x<10;++x)
            Render::draw_board_cell(f.context,p,y,x,b.field.cell(y,x));
        // Age the packet past QP's floor-dependent delay before arming it.
        Engine::GarbageService::add_buffer(f.context,p,b.field,4,true,4);
        assert(Engine::GarbageService::raise_ready_buffer(f.context,p,b.field)==0);
        assert(p.garbage.rising.remainingLines==4);
        assert(f.piece()==PlayResult::Locked);
        std::cerr<<"Live render completed: decisions="<<f.strategy.decisions<<"\n";
        Test::TerminalScreen screen; screen.feed(output.str());output.str({});
        assert(f.strategy.decisions==1 && !p.garbage.buffer.empty());
        assert(b.garbageHeight>0 && b.garbageHeight<4 && !p.presentation.paintedActivePose);
        for (int y=-Runtime::kDisplayHiddenRows;y<20;++y) for (int x=0;x<10;++x)
            Render::draw_board_cell(f.context,p,y,x,b.field.cell(y,x));
        Test::TerminalScreen expected;expected.feed(output.str());std::cout.rdbuf(previous);
        for (int y=1;y<=Runtime::kDisplayHiddenRows+20;++y) for (int x=21;x<=40;++x) {
            const auto actual=screen.at(y,x),wanted=expected.at(y,x);
            assert(actual.glyph==wanted.glyph && actual.background==wanted.background);
            if (actual.glyph!=" " && !actual.glyph.empty()) assert(actual.foreground==wanted.foreground);
        }
    }
}
void chatgpt_full_speed_reverse_gravity_screen(const std::string& savePath={}) {
    for (const bool ghost:{false,true}) {
        Fixture f(0);UserConfig::Settings user;
        user.qpGravity=2;user.autoplayMode=9;user.autoplayPps=0;user.autoplayWidth=300;
        user.ghost=ghost;user.lineClearDelay=0;user.entryDelay=0;
        f.context.config=Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay);
        auto& p=f.session->player;auto& b=p.gameplay.playfield;
        b.width=10;b.visibleHeight=20;b.nextCount=5;b.holdEnabled=true;
        p.pieces.allowedPieces=Piece::types_for_rotation_system(f.context.config.rotationSystem);
        p.pieces.random.seed(123);p.garbage.random.seed(456);
        Legacy::BotStrategy bot;f.context.attach_autoplay(bot);assert(bot.initialize(f.context));
        f.input.keyStateProvider=[](int){return false;};
        std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
        Engine::initialize_board(f.context,p);Engine::GarbageService::initialize(p,f.context.config.garbage.holePattern);
        Engine::initialize_board_timers(f.context,p);Engine::initialize_quick_play(f.context,p);
        for (int y=-Runtime::kDisplayHiddenRows;y<20;++y) for (int x=0;x<10;++x)
            Render::draw_board_cell(f.context,p,y,x,b.field.cell(y,x));
        f.replay.data=Replay::encode_config_prefix(f.context.config)+"{block_seed,123}{garbage_seed,456}}";
        Engine::GarbageService::add_buffer(f.context,p,b.field,6,true,6);
        Engine::GarbageService::raise_ready_buffer(f.context,p,b.field);
        Test::TerminalScreen screen;screen.feed(output.str());output.str({});int checked=0;
        const auto exit=Engine::run_piece_loop(f.context,p,[&](Runtime::Playfield& field,Piece::Definition piece,
            Runtime::PlayerSessionState& player,bool continuation) {
            if (checked>=80) return PlayResult::Retry;
            const auto result=App::run_piece(f.context,field,piece,player,f.loop,continuation);
            screen.feed(output.str());output.str({});
            if (result==PlayResult::Locked) {
                for (int y=-Runtime::kDisplayHiddenRows;y<20;++y) for (int x=0;x<10;++x)
                    Render::draw_board_cell(f.context,p,y,x,b.field.cell(y,x));
                Test::TerminalScreen expected;expected.feed(output.str());output.str({});
                for (int y=1;y<=Runtime::kDisplayHiddenRows+20;++y) for (int x=21;x<=40;++x) {
                    const auto actual=screen.at(y,x),wanted=expected.at(y,x);
                    if (actual.glyph!=wanted.glyph || actual.background!=wanted.background)
                        std::cerr<<"ChatGPT rGV screen mismatch at lock "<<checked<<" ghost="<<ghost<<" cell="<<y<<','<<x<<"\n";
                    assert(actual.glyph==wanted.glyph && actual.background==wanted.background);
                    if (actual.glyph!=" " && !actual.glyph.empty()) assert(actual.foreground==wanted.foreground);
                }
                ++checked;
            }
            return result;
        },&f.loop);
        Replay::add_replay(f.context,Replay::OperationKind::EndAll);
        if (!savePath.empty()) {std::ofstream file(savePath+std::to_string(ghost)+".txt");file<<f.replay.data;assert(file.good());}
        std::cout.rdbuf(previous);
        assert(checked==80 && exit==Engine::GameLoopExit::Retry);
        std::cerr<<"ChatGPT rGV full speed: "<<checked<<" checked locks; ghost="<<ghost<<"\n";
    }
}

void replay_repeated_types_keep_settled_cells() {
    Fixture f(0);auto& p=f.session->player;auto& b=p.gameplay.playfield;
    f.input.keyStateProvider=[](int) { return false; };
    f.context.config.field.width=b.width=10;f.context.config.field.height=b.visibleHeight=b.activeHeight=20;
    b.field.initialize(10,20);f.context.config.mode.replayMode=true;
    Replay::configure_playback(f.context,1024,{});
    auto piece=Piece::get_definition(Engine::current_piece(p));
    Test::TerminalScreen screen;
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    for (int y=-Runtime::kDisplayHiddenRows;y<20;++y) for (int x=0;x<10;++x)
        Render::draw_board_cell(f.context,p,y,x,b.field.cell(y,x));
    screen.feed(output.str());output.str({});
    for (int repeat=0;repeat<2;++repeat) {
        std::cerr<<"Replay render case: repeat="<<repeat<<"\n";
        auto movement=Engine::make_spawn_movement_state(10,piece.type());auto landing=movement.pose;
        while (piece.can_place(landing.translated(1,0),b.field)) ++landing.position.row;
        const auto pose=[](Piece::Pose at) {return std::to_string(at.position.row)+","+
            std::to_string(at.position.col)+","+std::to_string(at.rotation.index());};
        f.replay.operations=Replay::decode_operation_stream("{start_place,0}{put1,0,"+pose(movement.pose)+
            "}{move,0,"+pose(landing)+"}{end_move,0}{end_move_data,0,0,0,"+pose(landing)+
            "}{offset_data,0,0}{clear_line,0}{end_clear_line,0}{end_place_data,0,0.8,0,1,1,1,0}{end_place,0}",0);
        std::cerr<<"Replay operations="<<f.replay.operations.size()<<" first="<<f.replay.operations.front().timestamp.seconds()<<"\n";
        assert(App::playback_replay_piece(f.context,b.field,piece,p,movement,f.loop.input,
            [] {return PlayResult::ContinueFrame;})==PlayResult::ContinueFrame);
        std::cerr<<"Replay movement completed; remaining="<<f.replay.operations.size()<<"\n";
        assert(!p.presentation.activePose && !p.presentation.paintedActivePose);
        assert(Engine::finalize_locked_piece(f.context,b.field,piece,p,movement,0,
            [] {return PlayResult::ContinueFrame;})==PlayResult::Locked);
        screen.feed(output.str());output.str({});
        for (int y=-Runtime::kDisplayHiddenRows;y<20;++y) for (int x=0;x<10;++x)
            Render::draw_board_cell(f.context,p,y,x,b.field.cell(y,x));
        Test::TerminalScreen expected;expected.feed(output.str());output.str({});
        for (int y=1;y<=Runtime::kDisplayHiddenRows+20;++y) for (int x=21;x<=40;++x) {
            const auto actual=screen.at(y,x),wanted=expected.at(y,x);
            assert(actual.glyph==wanted.glyph && actual.background==wanted.background);
            if (actual.glyph!=" " && !actual.glyph.empty()) assert(actual.foreground==wanted.foreground);
        }
    }
    std::cout.rdbuf(previous);
}
}
int main(int argc,char** argv) {
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    full_cycle_cadence(20,.010,.002,.006,.008,8);
    full_cycle_cadence(4,.025,.004,.015,.020,4);
    full_speed_quick_play_redraws_and_settles_after_search_rises();
    locks_and_clears_inside_the_same_rolling_wave();
    replay_repeated_types_keep_settled_cells();
    chatgpt_full_speed_reverse_gravity_screen(argc>1 ? argv[1] : "");
    hard_drop_is_instant_when_simulation_is_off();overruns_and_controls();hold_uses_one_slot();handling_cycle_spends_only_remaining_pps_wait();
    std::cout.rdbuf(previous);
    std::cerr<<"autoplay_timing_test: OK\n";
}
