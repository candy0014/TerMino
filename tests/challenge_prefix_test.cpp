#include "tetr/challenges/challenges.h"
#include "tetr/app/application_state.h"
#include "tetr/app/init.h"
#include "tetr/app/game.h"
#include "tetr/app/place.h"
#include "tetr/config/normalization.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/challenge_prefix.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/session_state.h"
#include "../src/app/single_player_challenges.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
using namespace Challenges;
Definition only_quads() {
    auto d=*find("sprint-40");d.id="test-quads";
    d.stopRule=[] {return [](const Placement& p,const Result&,const Result&){return p.lines!=0 && p.lines!=4;};};
    return d;
}
Result result(const UserConfig::Settings& settings,int blocks,int quads,double time) {
    Result r;r.config=Runtime::normalize_user_config(settings,Runtime::GameMode::Zen);
    r.statistics.placedBlocks=blocks;r.statistics.totalLines=quads*4;r.statistics.totalAttack=quads*4;
    if(quads)r.statistics.clearStatistics[{0,4}][0]=quads;
    r.gameSeconds=time;r.realSeconds=time;return r;
}
void independent_prefixes() {
    auto quad=only_quads(),column=quad;column.id="test-columns";
    // Mutable rule state belongs to one attempt and can retain small histories.
    column.stopRule=[] {return [previous=-1](const Placement& p,const Result&,const Result&) mutable {
        const bool fail=previous==p.column;previous=p.column;return fail;
    };};
    UserConfig::Settings settings;quad.change(settings);const auto initial=result(settings,0,0,0);
    PrefixTracker tracker({quad,column},settings,initial);
    Placement p;p.lines=4;p.column=1;
    assert(!tracker.observe(p,result(settings,1,1,1)));
    p.column=1;assert(!tracker.observe(p,result(settings,2,2,2)));
    p.lines=1;assert(!tracker.observe(p,result(settings,3,2,3)));
    p.lines=4;p.column=3;assert(!tracker.observe(p,result(settings,4,3,4)));
    const auto* q=tracker.find(quad.id);const auto* c=tracker.find(column.id);
    assert(q && q->violated && q->boundary.placements==2 && q->result.statistics.placedBlocks==2);
    assert(q->result.statistics.totalLines==8 && q->result.statistics.clearStatistics.at({0,4})[0]==2);
    assert(c && c->violated && c->boundary.placements==1);
    const auto full=result(settings,4,3,4);auto candidate=tracker.result_for(quad,full);
    const auto score=quad.val(settings,*candidate);assert(score && score->progress==8 && score->value==2);
    auto undone=full;undone.undoCount=1;assert(!quad.val(settings,*tracker.result_for(quad,undone)));
    undone=full;undone.pauseCount=1;assert(!quad.val(settings,*tracker.result_for(quad,undone)));
    assert(tracker.result_for(*find("sprint-40"),full)->statistics.totalLines==12);
    assert(!PrefixTracker{}.result_for(quad,full));
    PrefixTracker selected({quad,column},settings,initial,quad.id);
    p.lines=1;assert(selected.observe(p,result(settings,1,0,1)));
    assert(selected.find(quad.id)->boundary.placements==0);
    assert(selected.find(quad.id)->result.statistics.clearStatistics.empty());
    // Each new attempt creates a new predicate, and the other rule's failure
    // never stops the explicitly selected quad challenge.
    PrefixTracker fresh({quad,column},settings,initial,quad.id);
    p.lines=4;p.column=2;
    assert(!fresh.observe(p,result(settings,1,1,1)));
    assert(!fresh.observe(p,result(settings,2,2,2)));
    assert(fresh.find(column.id)->violated && !fresh.find(quad.id)->violated);
    settings.fieldWidth=17;auto changed=initial;changed.config.field.width=17;
    PrefixTracker unqualified({quad},settings,changed,quad.id);
    assert(!unqualified.find(quad.id) && !unqualified.observe(p,full));
}
std::string recording(const std::vector<PrefixBoundary>& boundaries) {
    Runtime::RuntimeConfig config;
    return Replay::encode_config_prefix(config)+"{block_seed,123}{result_game_time,99}{result_real_time,100}"+
        Replay::encode_challenge_prefixes(boundaries)+"}"
        "{wall_time,2}{start_place,1}{wall_time,3}{end_place,1}"
        "{wall_time,4}{start_place,1}{wall_time,5}{end_place,1}"
        "{wall_time,6}{start_place,2}{wall_time,7}{end_place,2}{end_all,99}";
}
void replay_bounds() {
    const PrefixBoundary boundary{"test-quads",1,1,1,3};const auto original=recording({boundary});
    const auto clipped=Replay::prepare_challenge_replay(original,boundary.challengeId,1,true);
    assert(clipped.status==Replay::PrefixPreparationStatus::Ready && clipped.boundary->placements==1);
    Runtime::RuntimeConfig config;Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(clipped.recording,config,prefix));
    assert(prefix.blockSeed==123 && prefix.resultGameTime->seconds()==1 && prefix.resultRealTime->seconds()==3);
    auto ops=Replay::decode_operation_stream(clipped.recording,prefix.operationOffset);
    assert(ops.size()==3 && ops.front().kind==Replay::OperationKind::StartPlace);ops.pop();
    assert(ops.front().kind==Replay::OperationKind::EndPlace && ops.front().timestamp.seconds()==3);ops.pop();
    assert(ops.front().kind==Replay::OperationKind::EndAll && ops.front().timestamp.seconds()==3);
    assert(Replay::prepare_challenge_replay(original,"other",1,false).recording==original);
    auto zero=boundary;zero.placements=0;zero.gameSeconds=zero.realSeconds=0;
    const auto empty=Replay::prepare_challenge_replay(recording({zero}),zero.challengeId,1,true);
    assert(empty.status==Replay::PrefixPreparationStatus::Ready);
    assert(Replay::decode_config_prefix(empty.recording,config,prefix));
    ops=Replay::decode_operation_stream(empty.recording,prefix.operationOffset);
    assert(ops.size()==1 && ops.front().kind==Replay::OperationKind::EndAll && ops.front().timestamp.seconds()==0);
    // Missing, stale, duplicate or impossible caches request analysis. They
    // cannot silently fall back to the unrestricted original.
    auto impossible=boundary;impossible.placements=4;
    for(const auto& text:{recording({}),recording({boundary,boundary}),recording({impossible}),
                         recording({{"test-quads",0,1,1,3}})}) {
        const auto missing=Replay::prepare_challenge_replay(text,"test-quads",1,true);
        assert(missing.status==Replay::PrefixPreparationStatus::NeedsAnalysis && missing.recording.empty());
    }
    assert(Replay::prepare_challenge_replay(original,"test-quads",2,true).status==Replay::PrefixPreparationStatus::NeedsAnalysis);
    int analyses=0;
    const auto analyzed=Replay::prepare_challenge_replay(recording({}),"test-quads",1,true,[&](const std::string& full) {
        ++analyses;assert(full==recording({}));return std::optional<PrefixBoundary>{boundary};
    });
    assert(analyses==1 && analyzed.status==Replay::PrefixPreparationStatus::Ready && analyzed.boundary->placements==1);
    assert(Replay::prepare_challenge_replay("bad","test-quads",1,true).status==Replay::PrefixPreparationStatus::Invalid);
}
struct Rig {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    App::ApplicationState app;UserConfig::Settings settings;Input::Services input;Replay::ReplaySessionState replay;
    Rig() {
        context.attach_app(app);context.attach_user_settings(settings);context.attach_input(input);context.attach_replay(replay);
        app.primaryContext=&context;settings.keyCodes=UserConfig::default_key_codes();input.keyStateProvider=[](int){return false;};
        find("sprint-40")->change(settings);
        context.config=Runtime::normalize_user_config(settings,Runtime::GameMode::Zen);
    }
};
void placement_hook_and_save() {
    Rig r;auto& player=r.session->player;auto& board=player.gameplay.playfield;
    const auto directory=std::filesystem::temp_directory_path()/("termino_prefix_"+std::to_string(utc_now_milliseconds()));
    std::filesystem::create_directories(directory);r.settings.persistencePath=(directory/"player.ini").string();
    board.width=10;board.visibleHeight=20;board.activeHeight=20;
    player.pieces.allowedPieces=Piece::types_for_rotation_system(r.context.config.rotationSystem);
    Engine::initialize_board(r.context,player);
    r.replay.data=Replay::encode_config_prefix(r.context.config)+"}";
    // Exercise the real app observer and engine lock boundary with a test rule.
    App::begin_single_player_challenges(r.context);assert(r.app.challengePlacementObserver);
    auto d=only_quads();d.stopRule=[] {return [](const Placement&,const Result&,const Result&){return true;};};
    r.app.challengeRound->prefixes=PrefixTracker({d},r.settings,result(r.settings,0,0,0),d.id);
    r.app.challengeRound->selectedChallenge=d.id;
    auto piece=Piece::get_definition(Piece::Type{0});Engine::MovementState movement;
    movement.pose.position.col=4;
    while(piece.can_place(movement.pose.translated(1,0),board.field))++movement.pose.position.row;
    Render::begin_output_suppression();
    const auto outcome=Engine::finalize_locked_piece(r.context,board.field,piece,player,movement,0,[]{return Rules::PlayResult::ContinueFrame;});
    Render::end_output_suppression();
    assert(outcome==Rules::PlayResult::Dead && player.gameplay.scoring.placedBlocks==1);
    assert(r.replay.data.find("{end_place,")!=std::string::npos);
    assert(r.app.challengeRound->prefixes.find(d.id)->boundary.placements==0);
    player.timing.completedGameTime=Runtime::Duration::from_seconds(3);
    player.timing.completedRealTime=Runtime::Duration::from_seconds(4);
    App::finish_single_player_challenges(r.context,player);
    assert(player.gameplay.scoring.placedBlocks==0 && player.timing.completedGameTime->seconds()==0);
    assert(r.app.challengeRound->completedResult->statistics.placedBlocks==1);
    const auto saved=Replay::recording_for_save(r.context);
    assert(saved.find("{challenge_prefix,test-quads,1,0,")!=std::string::npos);
    Runtime::RuntimeConfig config;Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(saved,config,prefix) && prefix.resultGameTime->seconds()==3 && prefix.resultRealTime->seconds()==4);
    r.replay.undoCount=1;assert(Replay::recording_for_save(r.context).find("{challenge_prefix,")==std::string::npos);
    r.context.config.mode.replayMode=true;r.replay.loadedRecording=saved;
    assert(Replay::recording_for_save(r.context)==saved && Replay::recording_for_save(r.context)==saved);
    std::filesystem::remove_all(directory);
}
void empty_prefix_spawns_nothing() {
    Rig r;r.context.config.mode.replayMode=true;r.replay.challengePrefixActive=true;
    r.replay.operations=Replay::decode_operation_stream("{end_all,0}",0);
    App::PieceLoopState loop;auto& player=r.session->player;
    assert(App::run_piece(r.context,player.gameplay.playfield.field,Piece::get_definition(Piece::Type{0}),player,loop)==Rules::PlayResult::Dead);
    assert(player.gameplay.scoring.placedBlocks==0 && player.gameplay.scoring.roundPieceTypes.empty());
}
}
int main() {
    independent_prefixes();replay_bounds();placement_hook_and_save();empty_prefix_spawns_nothing();
    std::cerr<<"challenge prefix isolation, scoring, replay bounds and lock hook passed\n";
}
