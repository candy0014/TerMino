#include "tetr/app/post_game_interaction.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/engine/undo.h"
#include "tetr/engine/replay_application.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/engine/spike_presentation.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/session_state.h"
#include "tetr/render/board_renderer.h"
#include "tetr/runtime/game_context.h"
#include "../src/app/post_game_statistics.h"
#include "../src/app/settings_navigation.h"
#include "../src/app/init_player_setup.h"
#include "../src/app/ui_text.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <iostream>
#include <set>
#include <sstream>
#include <filesystem>
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif

namespace {
using D=Runtime::Duration;
struct Capture {
    std::ostringstream output;std::streambuf* old=std::cout.rdbuf(output.rdbuf());
    ~Capture() {std::cout.rdbuf(old);}
    std::string take() {auto result=output.str();output.str({});return result;}
};
struct Rig {
    Runtime::SessionState session;Runtime::RuntimeServices services;Runtime::GameContext context{session,services};
    Input::Services input;Replay::ReplaySessionState replay;
    Runtime::PlayerSessionState& p=session.player;
    Rig(int height=20) {
        UserConfig::Settings u;u.fieldWidth=10;u.fieldHeight=height;u.nextCount=0;
        u.lineClearDelay=0;u.entryDelay=0;
        context.config=Runtime::normalize_user_config(u,Runtime::GameMode::Zen);
        context.attach_input(input);context.attach_replay(replay);
        input.keyStateProvider=[](int) {return false;};
        auto& b=p.gameplay.playfield;b.width=10;b.visibleHeight=b.activeHeight=height;
        b.nextCount=0;b.holdEnabled=true;p.presentation.margin=30;
        p.pieces.allowedPieces=Piece::types_for_rotation_system(context.config.rotationSystem);
        p.pieces.random.seed(123);p.garbage.random.seed(456);p.attackRoundingRandom.seed(789);
        Engine::initialize_board(context,p);Engine::GarbageService::initialize(p,context.config.garbage.holePattern);
        Engine::initialize_board_timers(context,p);
    }
};
std::string metric(const App::PostGameStatistics& s,const std::string& name) {
    for (const auto& item:s.metrics) if (item.first==name) return item.second;
    return {};
}
void metrics_and_frozen_times() {
    Rig r;auto& s=r.p.gameplay.scoring;
    r.context.config.simulationPps=2.5;r.session.gameStartTime=Runtime::TimePoint{};
    r.session.simulationTime=Runtime::TimePoint::from_seconds(120);
    r.p.timing.realGameStart=r.context.real_now()-D::from_seconds(3);
    r.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    r.session.quickPlay.floor=5;r.session.quickPlay.altitude=600;
    s.totalHolds=10;s.totalAttack=240;s.placedBlocks=300;s.totalLines=100;s.maxCombo=6;s.maxB2B=4;
    App::freeze_post_game_times(r.context,r.p);
    const auto summary=App::make_post_game_statistics(r.context,r.p);
    assert(summary.metrics.size()==19);
    assert(metric(summary,"TIME")=="02:00.000" && metric(summary,"REAL TIME")=="00:03.000");
    assert(metric(summary,"FLOOR")=="5" && metric(summary,"ALTITUDE")=="600.00");
    assert(metric(summary,"ALTITUDE PER SECOND")=="5.000" && metric(summary,"HOLDS")=="10");
    assert(metric(summary,"ATTACK")=="240" && metric(summary,"ATTACK PER MINUTE")=="120.000");
    assert(metric(summary,"ATTACK PER PIECE")=="0.800" && metric(summary,"PIECES")=="300");
    assert(metric(summary,"PIECES PER SECOND")=="2.500" && metric(summary,"LINES")=="100");
    assert(metric(summary,"LINES PER MINUTE")=="50.000");
    assert(metric(summary,"MAXIMUM COMBO")=="5" && metric(summary,"MAXIMUM BACK-TO-BACK CHAIN")=="3");
    r.session.simulationTime=Runtime::TimePoint::from_seconds(999);
    r.p.timing.realGameStart=r.context.real_now()-D::from_seconds(100);
    App::freeze_post_game_times(r.context,r.p);
    assert(App::make_post_game_statistics(r.context,r.p).metrics==summary.metrics);
    r.context.config.garbage.mode=Runtime::GarbageMode::None;r.context.config.simulationPps=0;
    const auto ordinary=App::make_post_game_statistics(r.context,r.p);
    assert(ordinary.metrics.size()==15 && metric(ordinary,"REAL TIME").empty() && metric(ordinary,"FLOOR").empty());
    r.p.gameplay.scoring={};r.p.timing.completedGameTime=D{};
    const auto empty=App::make_post_game_statistics(r.context,r.p);
    assert(metric(empty,"TIME")=="00:00.000" && metric(empty,"ATTACK PER PIECE")=="0.000");
    assert(metric(empty,"ATTACK PER MINUTE")=="0.000" && metric(empty,"PIECES PER SECOND")=="0.000");
    r.p.timing.completedGameTime=D::from_seconds(59.9996);
    assert(metric(App::make_post_game_statistics(r.context,r.p),"TIME")=="01:00.000");
    // Replay speeds do not change either recorded game time or recorded wall time.
    r.context.config.mode.replayMode=true;r.context.config.simulationPps=2.5;
    r.p.timing.completedGameTime.reset();r.p.timing.completedRealTime.reset();
    r.replay.separateGameTime=true;r.replay.lastGameTimestamp=D::from_seconds(120);
    r.replay.lastConsumedTimestamp=D::from_seconds(3);
    Replay::configure_playback(r.context,1024,D::from_seconds(3));
    const auto replayFast=App::make_post_game_statistics(r.context,r.p);
    Replay::configure_playback(r.context,1./1024,D::from_seconds(3));
    const auto replaySlow=App::make_post_game_statistics(r.context,r.p);
    assert(replayFast.metrics==replaySlow.metrics);
    assert(metric(replayFast,"TIME")=="02:00.000" && metric(replayFast,"REAL TIME")=="00:03.000");
    r.replay.separateGameTime=false;
    assert(metric(App::make_post_game_statistics(r.context,r.p),"REAL TIME")=="N/A");
}
void saved_times_survive_replay_speed_and_end_screen_waits() {
    Rig live;live.context.config.simulationPps=2.5;
    live.p.timing.completedGameTime=D::from_seconds(120.123456789);
    live.p.timing.completedRealTime=D::from_seconds(3.987654321);
    live.replay.data=Replay::encode_config_prefix(live.context.config)+"{block_seed,123}{garbage_seed,456}}{end_all,121}";
    const auto original=live.replay.data,saved=Replay::recording_for_save(live.context);
    assert(live.replay.data==original);
    Runtime::RuntimeConfig config;Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(saved,config,prefix));
    assert(prefix.resultGameTime==live.p.timing.completedGameTime && prefix.resultRealTime==live.p.timing.completedRealTime);
    assert(saved.substr(prefix.operationOffset)=="{end_all,121}");
    Rig replay;replay.context.config=config;replay.context.config.mode.replayMode=true;
    replay.replay.resultGameTime=prefix.resultGameTime;replay.replay.resultRealTime=prefix.resultRealTime;
    Replay::configure_playback(replay.context,1024,D::from_seconds(121));
    assert(App::make_post_game_statistics(replay.context,replay.p).metrics==App::make_post_game_statistics(live.context,live.p).metrics);
    Replay::configure_playback(replay.context,1./1024,D::from_seconds(121));
    assert(App::make_post_game_statistics(replay.context,replay.p).metrics==App::make_post_game_statistics(live.context,live.p).metrics);
    Runtime::RuntimeConfig bad;Replay::ConfigPrefixResult invalid;
    assert(!Replay::decode_config_prefix("{{result_real_time,-1}}",bad,invalid));
    assert(!Replay::decode_config_prefix("{{result_game_time,nan}}",bad,invalid));
}
App::PostGameStatistics table_data(Rig& r) {
    const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    auto& counts=r.p.gameplay.scoring.clearStatistics;
    counts[{0,1}]={2,1,3};counts[{types.front().index(),1}]={1,2,1};
    counts[{21,3}]={1,0,0};counts[{5,0}]={0,2,1};counts[{21,39}]={0,1,0};counts[{5,80}]={1,0,0};
    return App::make_post_game_statistics(r.context,r.p);
}
void catalog_columns_categories_and_totals() {
    Rig r;const auto t=table_data(r);
    assert((t.lines==std::vector<int>{0,1,3,39,80}));
    std::set<std::string> names;for (const auto type:r.p.pieces.allowedPieces) names.insert(std::string(Piece::metadata(type).name));
    names.insert(std::string(Piece::metadata(Piece::Type{21}).name));
    assert(t.pieces.size()==names.size());
    for (const auto& name:names) assert(std::find(t.pieces.begin(),t.pieces.end(),name)!=t.pieces.end());
    assert((t.total==Runtime::ClearCounts{5,6,5}));
    assert((t.cells[0][1]==Runtime::ClearCounts{3,3,4}));
    assert((t.columnTotals[0]==Runtime::ClearCounts{0,2,1}));
    assert((t.columnTotals[3]==Runtime::ClearCounts{0,1,0}));
    const auto i5=std::find(t.pieces.begin(),t.pieces.end(),std::string(Piece::metadata(Piece::Type{21}).name));
    assert(i5!=t.pieces.end());
    assert((t.rowTotals[i5-t.pieces.begin()]==Runtime::ClearCounts{1,1,0}));
    for (const auto& entry:Piece::catalog()) if (entry.metadata.name==Piece::metadata(Piece::Type{21}).name &&
        entry.metadata.rotationSystem==Runtime::RotationSystem::AprilFools2026 &&
        entry.shapeIndex!=Piece::catalog_entry(Piece::Type{21}).shapeIndex) {
        r.p.gameplay.scoring.clearStatistics[{entry.type.index(),3}]={0,4,0};
        const auto separate=App::make_post_game_statistics(r.context,r.p);
        const auto normal=std::find(separate.pieces.begin(),separate.pieces.end(),std::string(entry.metadata.name));
        const auto fool=std::find(separate.pieces.begin(),separate.pieces.end(),std::string(entry.metadata.name)+" [Fool]");
        assert(normal!=separate.pieces.end() && fool!=separate.pieces.end());
        assert((separate.rowTotals[normal-separate.pieces.begin()]==Runtime::ClearCounts{1,1,0}));
        assert((separate.rowTotals[fool-separate.pieces.begin()]==Runtime::ClearCounts{0,4,0}));
    }
}
void round_libraries_and_max_spike() {
    Rig r;
    auto summary=App::make_post_game_statistics(r.context,r.p);
    assert(summary.pieces.size()==7 && metric(summary,"MAXIMUM SPIKE")=="0");
    r.p.pieces.allowedPieces={Piece::Type{21}};
    r.p.pieces.bag.clear();r.p.pieces.nextPreview.clear();
    r.p.gameplay.scoring.roundPieceTypes.insert(21);
    summary=App::make_post_game_statistics(r.context,r.p);
    assert(summary.pieces==std::vector<std::string>{std::string(Piece::metadata(Piece::Type{21}).name)});
    Rig fool;fool.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    fool.context.config.quickPlay.fool=Runtime::FoolMode::AprilFools2026;fool.session.quickPlay.floor=3;
    const auto library=Piece::types_for_rotation_system(Runtime::RotationSystem::AprilFools2026,false,3);
    assert(App::make_post_game_statistics(fool.context,fool.p).pieces.size()==library.size());
    Capture output;
    r.context.config.simulationPps=2.5;r.session.simulationTime=Runtime::TimePoint::from_seconds(0);
    Engine::present_spike_attack(r.context,r.p,4);
    r.session.simulationTime=Runtime::TimePoint::from_seconds(.5);
    Engine::present_spike_attack(r.context,r.p,6);
    r.session.simulationTime=Runtime::TimePoint::from_seconds(1.5);
    Engine::present_spike_attack(r.context,r.p,3);
    assert(r.p.gameplay.scoring.maxSpike==10 && r.p.presentation.spike.attack==3);
    Engine::refresh_spike_display(r.context,r.p);
    assert(metric(App::make_post_game_statistics(r.context,r.p),"MAXIMUM SPIKE")=="10");
    App::InitDetail::reset_player_board(r.p,r.context.config);
    assert(r.p.gameplay.scoring.maxSpike==0 && r.p.gameplay.scoring.roundPieceTypes.empty());
}

void allclear_and_scaled_attack_totals() {
    Capture output;
    for (bool reward:{false,true}) {
        Rig r;auto& b=r.p.gameplay.playfield;auto& field=b.field;
        r.context.config.attackRules.perfectClearEnabled=reward;
        r.context.config.garbage.mode=Runtime::GarbageMode::Survival;
        r.context.config.garbage.sendMultiplier=2;r.context.config.garbage.hitMultiplier=2;
        r.context.config.garbage.survivalApm=0;
        Engine::begin_undo_history(r.context);
        Engine::GarbageService::add_buffer(r.context,r.p,field,3);
        assert(r.p.gameplay.scoring.totalReceivedAttack==6);
        for (int y=16;y<20;++y) for (int x=0;x<10;++x) if (x!=4) field.set_cell(y,x,Runtime::BoardCell::piece(2));
        auto piece=Piece::get_definition(r.p.pieces.allowedPieces[0]);
        Engine::MovementState movement;movement.pose={{17,3},Runtime::Rotation{1}};
        assert(piece.can_place(movement.pose,field));
        assert(Engine::finalize_locked_piece(r.context,field,piece,r.p,movement,0,
            [] {return Rules::PlayResult::ContinueFrame;})==Rules::PlayResult::Locked);
        const auto& scoring=r.p.gameplay.scoring;
        assert(scoring.allClearCount==1 && scoring.totalAttack>=8 && scoring.totalOutgoingAttack==scoring.totalAttack-6);
        const auto stats=App::make_post_game_statistics(r.context,r.p);
        assert(metric(stats,"ALLCLEAR")=="1" && metric(stats,"TOTAL RECEIVED")=="6");
        assert(metric(stats,"TOTAL ATTACK")==std::to_string(scoring.totalOutgoingAttack));
        Engine::capture_undo_snapshot(r.context);assert(Engine::restore_previous_undo_snapshot(r.context));
        assert(r.p.gameplay.scoring.allClearCount==0 && r.p.gameplay.scoring.totalReceivedAttack==0);
    }
    for (double multiplier:{0.,.5,2.}) {
        Rig live,replay;live.context.config.garbage.hitMultiplier=multiplier;
        replay.context.config=live.context.config;replay.context.config.mode.replayMode=true;
        for (int i=0;i<2;++i) Engine::GarbageService::add_buffer(live.context,live.p,live.p.gameplay.playfield.field,3);
        const auto expected=static_cast<std::uint64_t>(6*multiplier);
        assert(live.p.gameplay.scoring.totalReceivedAttack==expected);
        auto ops=Replay::decode_operation_stream(live.replay.data,0);
        auto piece=Piece::get_definition(replay.p.pieces.allowedPieces[0]);
        while (!ops.empty()) {Engine::apply_replay_gameplay_event(replay.context,ops.front(),replay.p.gameplay.playfield.field,replay.p,piece);ops.pop();}
        assert(replay.p.gameplay.scoring.totalReceivedAttack==expected);
    }
    Rig qp;qp.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;qp.context.config.simulationPps=1;
    qp.context.config.garbage.hitMultiplier=2;qp.session.quickPlay.altitude=2500;qp.session.quickPlay.floor=10;
    qp.session.gameStartTime=Runtime::TimePoint{};qp.session.simulationTime=Runtime::TimePoint{};
    Engine::GarbageService::add_qp_opponent_attack(qp.context,qp.p,qp.p.gameplay.playfield.field,20);
    const auto received=qp.p.gameplay.scoring.totalReceivedAttack;
    assert(received>0 && !qp.session.quickPlay.progress.windupWarnings.empty());
    // Released segments are already part of the reception counted above.
    Engine::GarbageService::add_buffer(qp.context,qp.p,qp.p.gameplay.playfield.field,6,false,6,false,0);
    assert(qp.p.gameplay.scoring.totalReceivedAttack==received);
    Rig replay;replay.context.config=qp.context.config;replay.context.config.mode.replayMode=true;
    auto ops=Replay::decode_operation_stream(qp.replay.data,0);auto piece=Piece::get_definition(replay.p.pieces.allowedPieces[0]);
    while (!ops.empty()) {Engine::apply_replay_gameplay_event(replay.context,ops.front(),replay.p.gameplay.playfield.field,replay.p,piece);ops.pop();}
    assert(replay.p.gameplay.scoring.totalReceivedAttack==received);
    App::InitDetail::reset_player_board(qp.p,qp.context.config);
    assert(qp.p.gameplay.scoring.totalReceivedAttack==0 && qp.p.gameplay.scoring.allClearCount==0);
}

void actual_settlement_counts_large_clears_and_spins() {
    for (const int height:{20,40}) {
        Rig r(height);auto& field=r.p.gameplay.playfield.field;
        auto piece=Piece::get_definition(r.p.pieces.allowedPieces[0]);
        for (int y=1;y<height;++y) for (int x=0;x<10;++x) field.set_cell(y,x,Runtime::BoardCell::piece(2));
        auto movement=Engine::make_spawn_movement_state(10,piece.type());
        while (piece.can_place(movement.pose.translated(1,0),field)) ++movement.pose.position.row;
        assert(piece.can_place(movement.pose,field));
        assert(Engine::finalize_locked_piece(r.context,field,piece,r.p,movement,0,
            [] {return Rules::PlayResult::ContinueFrame;})==Rules::PlayResult::Locked);
        assert(r.p.gameplay.scoring.totalLines==height-1);
        assert((r.p.gameplay.scoring.clearStatistics.at({piece.type().index(),height-1})==Runtime::ClearCounts{1,0,0}));
        assert(App::make_post_game_statistics(r.context,r.p).lines.front()==height-1);
    }
    for (const bool mini:{false,true}) for (const bool clears:{false,true}) {
        Rig r;auto& field=r.p.gameplay.playfield.field;
        auto piece=Piece::get_definition(Piece::Type{5});
        Engine::MovementState movement;movement.pose={{clears ? 19 : 18,5},Runtime::Rotation{}};
        movement.lastRotation=Runtime::RotationRecord::from_kick(Piece::KickIndex{0});
        if (clears) {
            for (int x=0;x<10;++x) if (x<4 || x>6) field.set_cell(19,x,Runtime::BoardCell::piece(2));
            field.set_cell(18,4,Runtime::BoardCell::piece(2));
            if (!mini) field.set_cell(18,6,Runtime::BoardCell::piece(2));
        } else {
            field.set_cell(17,4,Runtime::BoardCell::piece(2));field.set_cell(19,6,Runtime::BoardCell::piece(2));
            field.set_cell(mini ? 19 : 17,mini ? 4 : 6,Runtime::BoardCell::piece(2));
        }
        assert(piece.can_place(movement.pose,field));
        assert(Engine::finalize_locked_piece(r.context,field,piece,r.p,movement,0,
            [] {return Rules::PlayResult::ContinueFrame;})==Rules::PlayResult::Locked);
        Runtime::ClearCounts wanted{};wanted[mini ? 2 : 1]=1;
        assert(r.p.gameplay.scoring.clearStatistics.at({5,int(clears)})==wanted);
    }
    Rig r;auto piece=Piece::get_definition(r.p.pieces.allowedPieces[0]);
    auto movement=Engine::make_spawn_movement_state(10,piece.type());auto& field=r.p.gameplay.playfield.field;
    while (piece.can_place(movement.pose.translated(1,0),field)) ++movement.pose.position.row;
    assert(Engine::finalize_locked_piece(r.context,field,piece,r.p,movement,0,
        [] {return Rules::PlayResult::ContinueFrame;})==Rules::PlayResult::Locked);
    assert(r.p.gameplay.scoring.placedBlocks==1 && r.p.gameplay.scoring.clearStatistics.empty());
}
void holds_reset_and_undo() {
    Rig r;r.context.config.mode.mode=Runtime::GameMode::Racing;r.context.config.mode.racingDistance=2;
    int calls=0;
    const auto outcome=Engine::run_piece_loop(r.context,r.p,[&](Runtime::Playfield&,Piece::Definition,
        Runtime::PlayerSessionState& p,bool continuation) {
        assert(continuation==bool(calls%2));
        if (calls++%2==0) return Rules::PlayResult::Hold;
        ++p.gameplay.scoring.placedBlocks;++p.gameplay.scoring.totalLines;return Rules::PlayResult::Locked;
    });
    assert(outcome==Engine::GameLoopExit::GoalReached && calls==4 && r.p.gameplay.scoring.totalHolds==2);
    r.context.config.mode.mode=Runtime::GameMode::Zen;
    r.p.gameplay.scoring.clearStatistics[{5,1}]={0,1,0};
    Engine::begin_undo_history(r.context);
    ++r.p.gameplay.scoring.placedBlocks;r.p.gameplay.scoring.totalHolds=9;
    r.p.gameplay.scoring.clearStatistics[{5,1}][1]=8;Engine::capture_undo_snapshot(r.context);
    assert(Engine::restore_previous_undo_snapshot(r.context));
    assert(r.p.gameplay.scoring.totalHolds==2);
    assert((r.p.gameplay.scoring.clearStatistics.at({5,1})==Runtime::ClearCounts{0,1,0}));
    App::InitDetail::reset_player_board(r.p,r.context.config);
    assert(r.p.gameplay.scoring.totalHolds==0 && r.p.gameplay.scoring.clearStatistics.empty());
}
void equal_screens(const Test::TerminalScreen& a,const Test::TerminalScreen& b,Render::TerminalSize size) {
    for (int y=1;y<=size.rows;++y) for (int x=1;x<=size.columns;++x) {
        const auto actual=a.at(y,x),wanted=b.at(y,x);
        assert(actual.glyph==wanted.glyph && actual.background==wanted.background);
        if (actual.glyph!=" " && !actual.glyph.empty()) assert(actual.foreground==wanted.foreground);
    }
}
void rendering_navigation_scrolling_and_resize() {
    Rig r;const auto data=table_data(r);App::PostGameStatisticsView view(data);
    Capture output;Test::TerminalScreen screen;
    view.next_page();view.render(r.context,"footer",{80,24});screen.feed(output.take());
    assert(screen.text().find("RESULTS")==std::string::npos && screen.text().find("TIME")!=std::string::npos);
    assert(screen.text().find("footer")==std::string::npos && screen.text().find("Tab:")==std::string::npos);
    view.next_page();assert(view.page()==App::PostGamePage::Clears);
    for (const auto size:{Render::TerminalSize{80,24},{32,12},{28,10},{120,40},{4,3},{80,24}}) {
        for (const auto delta:{std::pair<int,int>{0,0},{75,4},{200,200},{-180,-3},{-200,-200}}) {
            view.move(delta.first,delta.second);view.render(r.context,"footer",size);screen.feed(output.take());
            assert(view.selected_row()>=0 && view.selected_row()<=int(data.pieces.size()));
            assert(view.selected_column()>=0 && view.selected_column()<=int(data.lines.size()));
            view.invalidate();view.render(r.context,"footer",size);
            Test::TerminalScreen fresh;fresh.feed(output.take());equal_screens(screen,fresh,size);
            if (size.columns>=28 && size.rows>=10) {
                assert(screen.text().find("Selected:")==std::string::npos);
                assert(screen.text().find("CLEAR STATISTICS")==std::string::npos);
                assert(screen.text().find("SUM")!=std::string::npos);
                bool highlighted=false;
                const auto accent=Render::ui_colors(Render::ColorRole::Accent,false).foreground;
                for (int y=1;y<=size.rows;++y) for (int x=1;x<=size.columns;++x)
                    highlighted=highlighted || screen.at(y,x).background==accent;
                assert(highlighted); // Includes the pinned total cell in a ten-row window.
            }
        }
    }
    view.next_page();assert(view.page()==App::PostGamePage::Board);
    view.render(r.context,"footer",{80,24});assert(output.take().empty());
    view.next_page();view.render(r.context,"footer",{80,24});screen.feed(output.take());
    assert(screen.text().find("TIME")!=std::string::npos && screen.text().find("CLEAR STATISTICS")==std::string::npos);
    std::set<int> down;r.input.keyStateProvider=[&](int key) {return down.count(key)>0;};
    App::Settings::NavigationInput navigation(r.context);App::PostGameStatisticsView keys(data,data);
    using K=App::Settings::NavigationKey;using N=App::Settings::NavigationInput;
    down.insert(N::key_code(K::Tab));keys.poll(navigation);keys.poll(navigation);
    assert(keys.page()==App::PostGamePage::Summary);
    down.clear();keys.poll(navigation);down.insert(N::key_code(K::Tab));keys.poll(navigation);
    assert(keys.page()==App::PostGamePage::Clears);
    down.clear();keys.poll(navigation);down.insert(N::key_code(K::Down));down.insert(N::key_code(K::Right));keys.poll(navigation);
    assert(keys.selected_row()==1 && keys.selected_column()==1);
    assert(keys.reserves(N::key_code(K::Left)));
    down.clear();keys.poll(navigation);down.insert(N::key_code(K::PageDown));keys.poll(navigation);
    assert(keys.player_index()==1);
}
void summary_single_column_order_and_save_overlay() {
    Rig r;r.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;r.context.config.simulationPps=1;
    const auto data=App::make_post_game_statistics(r.context,r.p);
    const std::vector<std::string> expected{"TIME","REAL TIME","FLOOR","ALTITUDE","ALTITUDE PER SECOND",
        "ATTACK","ATTACK PER MINUTE","ATTACK PER PIECE","TOTAL ATTACK","TOTAL RECEIVED","MAXIMUM SPIKE",
        "PIECES","PIECES PER SECOND","LINES","LINES PER MINUTE","ALLCLEAR","HOLDS","MAXIMUM COMBO","MAXIMUM BACK-TO-BACK CHAIN"};
    assert(data.metrics.size()==expected.size());
    for (std::size_t i=0;i<expected.size();++i) assert(data.metrics[i].first==expected[i]);
    for (const auto size:{Render::TerminalSize{120,40},{80,24},{48,16},{28,10}}) {
        App::PostGameStatisticsView view(data);view.next_page();Capture output;Test::TerminalScreen screen;
        int boxColumn=-1;
        for (int index=0;index<int(expected.size());++index) {
            assert(view.selected_metric()==index);view.render(r.context,"footer",size);screen.feed(output.take());
            assert(screen.text().find(index==18 ? "MAXIMUM B2B" : expected[index])!=std::string::npos);
            for (int y=1;y<=size.rows;++y) for (int x=1;x<=size.columns;++x) if (screen.at(y,x).glyph=="┌") {
                if (boxColumn<0) boxColumn=x;
                assert(x==boxColumn && (screen.at(y,x).foreground==Test::TerminalScreen::Color{237,109,0}));
            }
            view.move(1,0);
        }
        view.show_replay_save_prompt("default.txt","中文很长的回放名字abcdef.txt",30);
        view.render(r.context,"footer",size);screen.feed(output.take());
        assert(screen.text().find("Save Replay")!=std::string::npos);
        view.finish_replay_save_prompt("Replay saved: replay/test.txt");view.render(r.context,"footer",size);screen.feed(output.take());
        assert(screen.text().find("Save Replay")==std::string::npos);
        view.invalidate();view.render(r.context,"footer",size);Test::TerminalScreen fresh;fresh.feed(output.take());equal_screens(screen,fresh,size);
    }
}
#ifdef __linux__
void actual_save_stays_on_statistics_page(int page,bool local,int action) {
    Rig first,second;Capture output;Test::TerminalScreen screen;
    first.replay.data="{{Model,0}}";second.replay.data="{{Model,0}}";
    auto codes=UserConfig::default_key_codes();Input::refresh_bindings(first.input,codes);Input::refresh_bindings(second.input,codes);
    const int retry=codes[8],save=codes[10],tab=App::Settings::NavigationInput::key_code(App::Settings::NavigationKey::Tab);
    const std::string name="statistics_中文_"+std::to_string(getpid())+"_"+std::to_string(page)+"_"+std::to_string(local)+".txt";
    const auto path=std::filesystem::path("replay")/name;assert(!std::filesystem::exists(path));
    const std::string typed=action==0 ? name+"\n" : action==1 ? "\x1b" : "../outside.txt\n";
    int descriptors[2];assert(pipe(descriptors)==0);const int saved=dup(STDIN_FILENO);assert(saved>=0);
    assert(dup2(descriptors[0],STDIN_FILENO)>=0);assert(write(descriptors[1],typed.data(),typed.size())==ssize_t(typed.size()));
    int step=0;bool fired=false,sawDialog=false;
    const std::string title=page==1 ? "TIME & ALTITUDE" : "PIECE";
    first.input.keyStateProvider=[&](int key) {
        if (key==tab) return step==1 || (page==2 && step==3);
        if (key==save) {
            if (step==2*page && !fired) {fired=true;return true;}
            if (fired && !sawDialog) {
                const auto bytes=output.take();screen.feed(bytes);
                assert(bytes.find("\x1b[2J")==std::string::npos);
                assert(screen.text().find(title)!=std::string::npos && screen.text().find("Custom replay name:")!=std::string::npos);
                sawDialog=true;
            }
            return false;
        }
        if (key==retry) {
            screen.feed(output.take());
            if (fired) {
                assert(sawDialog && screen.text().find(title)!=std::string::npos);
                assert(screen.text().find(action==0 ? "Replay saved:" : action==1 ? "Replay save cancelled." : "Invalid file name.")!=std::string::npos);
                return true;
            }
            ++step;
        }
        return false;
    };
    if (local) App::run_local_versus_post_game_interaction(first.context,second.context);
    else App::run_post_game_interaction(first.context,first.p);
    assert(fired && sawDialog);
    assert(dup2(saved,STDIN_FILENO)>=0);close(saved);close(descriptors[0]);close(descriptors[1]);std::cin.clear();
    assert(std::filesystem::exists(path)==(action==0));
    if (action==0) {std::ifstream file(path);const std::string data((std::istreambuf_iterator<char>(file)),{});assert(data.find("end_all")!=std::string::npos);file.close();std::filesystem::remove(path);}
}
#endif

void actual_end_screen_cycle(bool replay,bool network,bool local) {
    Rig first,second;Network::NetworkServices connection;
    first.context.config.mode.replayMode=replay;
    second.context.config.mode.replayMode=replay;
    if (network) {first.context.attach_network(connection);first.context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;first.p.presentation.roundEndHeadline="You lost this game!";}
    if (replay) {Replay::configure_playback(first.context,1024,D::from_seconds(10));Replay::finish_playback(first.context,first.context.now());}
    first.p.gameplay.scoring.totalAttack=12;second.p.gameplay.scoring.totalAttack=99;
    auto codes=UserConfig::default_key_codes();const int retry=codes[8],tab=App::Settings::NavigationInput::key_code(App::Settings::NavigationKey::Tab);
    Input::refresh_bindings(first.input,codes);
    Capture output;Test::TerminalScreen screen;int step=0;
    first.input.keyStateProvider=[&](int key) {
        if (key==retry) {
            screen.feed(output.take());
            if (step==1 || step==2 || step==3) assert(screen.text().find("TIME & ALTITUDE")!=std::string::npos);
            if (step==4 || step==5 || step==6) assert(screen.text().find("PIECE")!=std::string::npos);
            if (step==7) {
                assert(screen.text().find("PIECE")==std::string::npos);
                if (network) assert(screen.text().find("You lost this game!")!=std::string::npos);
            }
            return step++>=9;
        }
        return key==tab && (step==1 || step==2 || step==4 || step==7);
    };
    if (local) App::run_local_versus_post_game_interaction(first.context,second.context);
    else App::run_post_game_interaction(first.context,first.p);
    assert(step==10);
    assert(first.p.timing.completedGameTime && first.p.timing.completedRealTime);
    if (replay) assert(first.replay.playback.finished && first.replay.playback.speed==1024);
}
}
int main(int argc,char** argv) {
    if (argc>1) {
        Rig r;r.p.gameplay.scoring.totalAttack=240;r.p.gameplay.scoring.placedBlocks=300;
        r.p.gameplay.scoring.totalLines=100;r.p.gameplay.scoring.maxSpike=28;
        r.p.timing.completedGameTime=D::from_seconds(120);r.p.timing.completedRealTime=D::from_seconds(70);
        r.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;r.context.config.simulationPps=2.5;
        r.context.session.quickPlay.floor=5;r.context.session.quickPlay.altitude=600;
        App::PostGameStatisticsView view(table_data(r));
        App::UiText::set_language(App::UiText::Language::English);
        for (int page=0;page<2;++page) {
            view.next_page();Test::TerminalScreen screen;
            {Capture output;view.render(r.context,"[Esc] Retry   [S] Settings   [F6] Save replay",{80,24});screen.feed(output.take());}
            std::cout<<screen.text()<<"\n";
        }
        return 0;
    }
    round_libraries_and_max_spike();allclear_and_scaled_attack_totals();
    const auto language=App::UiText::language();App::UiText::set_language(App::UiText::Language::English);
    Capture output;
    metrics_and_frozen_times();saved_times_survive_replay_speed_and_end_screen_waits();catalog_columns_categories_and_totals();
    actual_settlement_counts_large_clears_and_spins();holds_reset_and_undo();
    rendering_navigation_scrolling_and_resize();
    actual_end_screen_cycle(false,false,false);actual_end_screen_cycle(true,false,false);
    summary_single_column_order_and_save_overlay();
#ifdef __linux__
    for (int page:{1,2}) for (bool local:{false,true}) for (int action:{0,1,2}) actual_save_stays_on_statistics_page(page,local,action);
#endif
    actual_end_screen_cycle(false,true,false);actual_end_screen_cycle(false,false,true);
    actual_end_screen_cycle(true,false,true);
    App::UiText::set_language(language);
    std::cerr<<"post_game_statistics_test: metrics, settlement, undo, catalog, scrolling, replay/network/local screens OK\n";
}
