#include "tetr/frontend/text_input.h"
#include "tetr/presentation/board_view.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/runtime_config.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace TetrTests::frontend_snapshot_test {
namespace {
Runtime::Duration seconds(double n) { return Runtime::Duration::from_seconds(n); }

struct Fixture {
    std::unique_ptr<Runtime::PlayerSessionState> owned = std::make_unique<Runtime::PlayerSessionState>();
    Runtime::PlayerSessionState& player = *owned;
    Runtime::RuntimeConfig config;
    Fixture() {
        auto& board = player.gameplay.playfield;
        board.width = 10; board.visibleHeight = board.activeHeight = 20;
        board.holdEnabled = true; board.nextCount = 2;
        board.field.initialize(10, 20);
        config.field.ghost = true;
        player.presentation.activePieceType = Piece::Type::from_index(5);
        player.presentation.activePose = Piece::spawn_pose(10, *player.presentation.activePieceType);
        player.presentation.username = "keyboard player";
        player.gameplay.hold.piece = Piece::Type::from_index(0);
        player.pieces.nextPreview = {Piece::Type::from_index(1), Piece::Type::from_index(2)};
    }
    Presentation::BoardView capture(double elapsed) {
        return Presentation::capture_board_view(config, player, player.gameplay.playfield.field, seconds(elapsed));
    }
};

void immutable_snapshot() {
    Fixture f;
    auto& field = f.player.gameplay.playfield.field;
    field.set_cell(19, 0, Runtime::BoardCell::piece(3));
    field.set_cell(19, 1, Runtime::BoardCell::timed_solid(4));
    f.player.gameplay.scoring.totalLines = 12;
    f.player.gameplay.scoring.totalAttack = 8;
    f.player.gameplay.scoring.score = 2000;
    const auto originalPose = *f.player.presentation.activePose;
    const auto originalNext = f.player.pieces.nextPreview;
    const auto view = f.capture(2);
    assert(view.width == 10 && view.bottom == 20 && view.cells.size() == static_cast<std::size_t>((view.bottom-view.firstRow)*10));
    assert(view.at(19, 0).value == Runtime::BoardCell::piece(3));
    assert(view.at(19, 1).value.timed_countdown() == 4);
    assert(view.active && view.ghost && view.ghost->pose.position.row > view.active->pose.position.row);
    assert(view.next == originalNext && view.nextCount==2 && view.hold == f.player.gameplay.hold.piece && view.holdEnabled);
    assert(view.lines == 12 && view.attack == 8 && view.score == 2000 && view.elapsed == seconds(2));
    assert(f.player.presentation.activePose->position.row == originalPose.position.row);
    assert(f.player.pieces.nextPreview == originalNext);
    field.set_cell(19, 0, Runtime::BoardCell::empty());
    assert(view.at(19, 0).value == Runtime::BoardCell::piece(3));
    bool rejected = false;
    try { (void)view.at(20, 0); } catch (const std::out_of_range&) { rejected = true; }
    assert(rejected);
}

void visibility_and_replay() {
    Fixture f;
    auto& field = f.player.gameplay.playfield.field;
    field.set_cell(19, 0, Runtime::BoardCell::piece(3));
    field.mark_cell_lock_time(19, 0, seconds(1));
    field.set_cell(19, 1, Runtime::BoardCell::garbage());
    f.config.field.invisible = true;
    f.config.field.invisibilityDelay = seconds(2);
    assert(f.capture(2.9).at(19, 0).appearance == Presentation::CellAppearance::Visible);
    const auto hidden = f.capture(3);
    assert(hidden.at(19, 0).appearance == Presentation::CellAppearance::Empty);
    assert(hidden.at(19, 0).value.empty_cell());
    assert(hidden.at(19, 1).appearance == Presentation::CellAppearance::Visible);
    f.config.challengeRules = Runtime::ChallengeRules::Blind;
    assert(!f.capture(3).active && !f.capture(3).ghost);
    f.config.mode.replayMode = true;
    const auto replay = f.capture(3);
    assert(replay.at(19, 0).appearance == Presentation::CellAppearance::Dimmed);
    assert(replay.at(19, 0).value == Runtime::BoardCell::piece(3));
    assert(replay.active && replay.active->appearance == Presentation::CellAppearance::Dimmed);
    f.config.challengeRules = Runtime::ChallengeRules::Instinct;
    f.player.presentation.activePieceBorn = seconds(3);
    assert(f.capture(3.1).active->appearance == Presentation::CellAppearance::Visible);
    assert(f.capture(3.21).active->appearance == Presentation::CellAppearance::Dimmed);
    f.config.field.periodicInvisible = true;
    f.player.presentation.invisibleFlash = true;
    assert(f.capture(5.5).at(19, 0).appearance == Presentation::CellAppearance::Visible);
    f.player.gameplay.hold.remoteAvailable = false;
    assert(!f.capture(4).holdAvailable);
}

void text_input_lifecycle() {
    struct Session final : Frontend::TextInputSession {
        int begins = 0, ends = 0;
        void begin_text_input() override { ++begins; }
        void end_text_input() override { ++ends; }
    } session;
    try {
        Frontend::TextInputScope scope(&session);
        assert(session.begins == 1 && session.ends == 0);
        throw std::runtime_error("cancelled text entry");
    } catch (const std::runtime_error&) {}
    assert(session.begins == 1 && session.ends == 1);
    Frontend::TextInputScope withoutWindow(nullptr);
}
void upper_ten_rows_and_markers() {
    Fixture f;auto& field=f.player.gameplay.playfield.field;
    field.set_cell(-10,1,Runtime::BoardCell::piece(0));
    field.set_cell(-9,2,Runtime::BoardCell::piece(1));
    field.set_cell(-11,3,Runtime::BoardCell::piece(2));
    f.config.display.showSpawnPosition=true;
    f.player.presentation.spawnMarkerCells={{-10,1},{-9,2}};
    auto view=f.capture(0);
    assert(view.firstRow==-10 && view.at(-10,1).value==Runtime::BoardCell::piece(0));
    assert(view.at(-9,2).value==Runtime::BoardCell::piece(1));
    assert(view.at(-10,1).hint && view.at(-9,2).hint);
    bool rejected=false;try{(void)view.at(-11,3);}catch(const std::out_of_range&){rejected=true;}
    assert(rejected);
    f.config.field.invisible=true;f.config.field.invisibilityDelay=seconds(0);
    view=f.capture(1);assert(view.at(-10,1).appearance==Presentation::CellAppearance::Empty);
    assert(view.at(-10,1).value.empty_cell() && view.at(-10,1).hint);
    f.config.display.showSpawnPosition=false;assert(!f.capture(1).at(-10,1).hint);
    f.config.field.invisible=false;f.player.timing.remoteGameTime=seconds(1);
    assert(f.capture(1).at(-10,1).value.empty_cell());
    assert(f.capture(1).at(-9,2).value.empty_cell());
}
void requested_hidden_region() {
    Fixture f;auto& field=f.player.gameplay.playfield.field;
    field.set_cell(-51,3,Runtime::BoardCell::piece(0));
    field.set_cell(-100,2,Runtime::BoardCell::piece(1));
    const auto capture=[&](int rows){return Presentation::capture_board_view(
        f.config,f.player,field,seconds(0),rows);};
    auto view=capture(51);assert(view.firstRow==-51 && view.at(-51,3).value.occupied());
    view=capture(999);assert(view.firstRow==-100 && view.at(-100,2).value.occupied());
    assert(capture(-1).firstRow==0 && capture(0).cells.size()==200);
    assert(f.capture(0).firstRow==-10);
    f.config.field.invisible=true;f.config.field.invisibilityDelay=seconds(0);
    assert(capture(51).at(-51,3).value.empty_cell());
}
void opacity_curves() {
    Fixture f;auto& field=f.player.gameplay.playfield.field;
    field.set_cell(19,0,Runtime::BoardCell::piece(3));field.mark_cell_lock_time(19,0,seconds(1));
    field.set_cell(19,1,Runtime::BoardCell::garbage());
    f.config.field.invisible=true;f.config.field.invisibilityDelay=seconds(2);
    for(const auto& sample:std::vector<std::pair<double,float>>{{1,1},{2,1},{2.25,.75f},{2.5,.5f},{2.75,.25f},{3,0},{4,0},{2.5,.5f}}) {
        const auto view=f.capture(sample.first);assert(std::abs(view.at(19,0).opacity-sample.second)<1e-6f);
        assert(view.at(19,1).opacity==1);
        if(!sample.second)assert(view.at(19,0).value.empty_cell());
    }
    f.config.field.periodicInvisible=true;f.config.field.invisibilityDuration=seconds(3);
    for(const auto& sample:std::vector<std::pair<double,float>>{{4.99,0},{5,0},{5.5,.5f},{6,1},{6.5,1},{7,1},{7.5,.5f},{8,0},{10,0},{10.5,.5f}})
        assert(std::abs(f.capture(sample.first).at(19,0).opacity-sample.second)<1e-6f);
    f.config.mode.replayMode=true;
    assert(std::abs(f.capture(5.5).at(19,0).opacity-.675f)<1e-6f);
    assert(f.capture(8).at(19,0).appearance==Presentation::CellAppearance::Dimmed);
    f.config.mode.replayMode=false;
    f.player.presentation.remoteFieldVisibility=Runtime::FieldVisibility{};
    assert(f.capture(4).at(19,0).opacity==1); // Peer visibility overrides local invisibility.
    f.config.field.invisible=false;
    f.player.presentation.remoteFieldVisibility=Runtime::FieldVisibility{true,false,seconds(4),seconds(5),seconds(3)};
    assert(f.capture(4).at(19,0).opacity==.5f);
    f.player.presentation.remoteFieldVisibility->periodic=true;
    assert(f.capture(7.5).at(19,0).opacity==.5f);
}
void finished_invisible_field() {
    Fixture f;auto& player=f.player;auto& field=player.gameplay.playfield.field;
    const auto piece=Runtime::BoardCell::piece(3),garbage=Runtime::BoardCell::garbage();
    field.set_cell(-8,0,piece);field.set_cell(19,0,piece);field.set_cell(19,1,garbage);
    const auto check=[&](bool finished) {
        const auto view=f.capture(9);
        const auto appearance=finished?Presentation::CellAppearance::Visible:f.config.mode.replayMode?
            Presentation::CellAppearance::Dimmed:Presentation::CellAppearance::Empty;
        for(const int row:{-8,19}) {
            const auto& cell=view.at(row,0);
            assert(Presentation::board_cell_visible(f.config,player,row,0,piece,seconds(9))==finished);
            assert(Presentation::board_cell_appearance(f.config,player,row,0,piece,seconds(9))==appearance);
            assert(cell.appearance==appearance);
            assert(cell.opacity==(finished?1.f:f.config.mode.replayMode?.35f:0.f));
            assert(cell.value==((finished || f.config.mode.replayMode)?piece:Runtime::BoardCell::empty()));
        }
        assert(view.at(19,2).appearance==Presentation::CellAppearance::Empty && view.at(19,2).opacity==0);
        assert(!Presentation::board_cell_visible(f.config,player,19,2,Runtime::BoardCell::empty(),seconds(9)));
    };
    for(bool remote:{false,true})for(bool periodic:{false,true})for(bool replay:{false,true}) {
        f.config.field.invisible=!remote;f.config.field.periodicInvisible=periodic;
        f.config.field.invisibilityDelay=seconds(2);f.config.mode.replayMode=replay;
        if(remote)player.presentation.remoteFieldVisibility=Runtime::FieldVisibility{true,periodic,seconds(2),seconds(5),seconds(1)};
        else player.presentation.remoteFieldVisibility.reset();
        player.timing.completedGameTime.reset();check(false);
        player.timing.completedGameTime=seconds(9);check(true);
        // Retrying or seeking back restores the original visibility rule.
        player.timing.completedGameTime.reset();check(false);
    }
    f.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    f.config.quickPlay.invisible=Runtime::ModifierMode::Reverse;
    player.gameplay.playfield.garbageHeight=6;
    assert(!Presentation::board_cell_visible(f.config,player,19,1,garbage,seconds(9)));
    player.timing.completedGameTime=seconds(9);
    assert(Presentation::board_cell_visible(f.config,player,19,1,garbage,seconds(9)));
    assert(f.capture(9).at(19,1).opacity==1);
}
void complete_hud_projection() {
    Fixture f;auto& p=f.player;
    f.config.mode.goal=Runtime::GameGoal::Pieces;f.config.mode.pieceTarget=50;
    p.gameplay.scoring.placedBlocks=12;p.gameplay.scoring.wins=3;
    p.garbage.buffer.emplace_back(4,Runtime::TimePoint::from_seconds(5));
    f.config.quickPlay.allSpin=Runtime::AllSpinMode::Extended;
    p.gameplay.quickPlay.operations.push_back({5,1});
    p.presentation.eventSpinPiece=Piece::Type::from_index(5);p.presentation.eventMini=true;
    p.presentation.eventClearLines=2;p.presentation.eventPerfectClear=true;
    f.config.display.showSpawnPosition=true;p.presentation.spawnMarkerCells.push_back({0,4});
    auto view=f.capture(2);
    assert(view.goal.type==Runtime::GameGoal::Pieces && view.goal.target==50 && view.goal.progress==12);
    assert(view.garbage==std::vector<int>{4} && view.wins==3);
    assert(view.spinPiece.valid() && view.mini && view.clearLines==2 && view.perfectClear);
    assert(view.spinHistory.size()==1 && view.at(0,4).hint);
    p.gameplay.quickPlay.operations.clear();assert(view.spinHistory.size()==1);
    f.config.challengeRules=Runtime::ChallengeRules::StageMaster;p.gameplay.scoring.challengeStages=2;
    assert(f.capture(2).stageTask=="2 SPINS");
}
void goal_cues() {
    Fixture f;auto& p=f.player;auto& mode=f.config.mode;
    mode.racingDistance=mode.garbageTarget=mode.pieceTarget=mode.attackTarget=mode.allClearTarget=22;
    auto& score=p.gameplay.scoring;
    for(const auto type:{Runtime::GameGoal::Lines,Runtime::GameGoal::Garbage,Runtime::GameGoal::Pieces,
        Runtime::GameGoal::Attack,Runtime::GameGoal::AllClear}) {
        mode.goal=type;
        for(const int done:{1,2,3,21,22,23,2}) {
            score.totalLines=score.totalGarbageLines=score.placedBlocks=score.totalAttack=score.allClearCount=done;
            const auto cue=f.capture(0).goal;
            assert(cue.remaining==std::max(0,22-done));
            assert(cue.markerRows==(type!=Runtime::GameGoal::AllClear && done>=2 && done<22?22-done:0));
            assert(!cue.unit.empty());
        }
    }
    mode.goal=Runtime::GameGoal::Time;mode.blitzTime=seconds(22);
    for(const auto time:{1.,1.99,2.,2.99,3.,21.99,22.,25.}) {
        const auto cue=f.capture(time).goal;
        const int remaining=static_cast<int>(std::max(0.,std::ceil(22-time)));
        assert(cue.remaining==remaining && cue.markerRows==(remaining<=20?remaining:0));
    }
    p.gameplay.playfield.activeHeight=8;
    assert(f.capture(13.99).goal.markerRows==0 && f.capture(14).goal.markerRows==8);
    auto map=std::make_shared<Runtime::MapDefinition>();map->rows[19][0]='G';map->rows[19][1]='T';
    f.config.initialMap=map;mode.goal=Runtime::GameGoal::InitialCells;
    auto& field=p.gameplay.playfield.field;
    for(int col=0;col<2;++col){field.set_cell(19,col,Runtime::BoardCell::garbage());field.mark_initial_cell(19,col);}
    auto cue=f.capture(0).goal;assert(cue.remaining==2 && cue.markerRows==0 && cue.unit=="C");
    field.set_cell(19,0,Runtime::BoardCell::empty());cue=f.capture(0).goal;assert(cue.remaining==1 && cue.markerRows==0);
    field.set_cell(19,1,Runtime::BoardCell::empty());assert(f.capture(0).goal.markerRows==0);
    mode.goal=Runtime::GameGoal::None;assert(f.capture(0).goal.markerRows==0);
    mode.mode=Runtime::GameMode::Racing;mode.mapGoal=Runtime::MapGoal::AllClear;
    score.allClearCount=0;score.mapAllClearReached=true;
    assert(f.capture(0).goal.type==Runtime::GameGoal::AllClear && f.capture(0).goal.markerRows==0);
    mode.mode=Runtime::GameMode::Zen;mode.goal=Runtime::GameGoal::AllClear;
    score.allClearCount=std::numeric_limits<std::uint64_t>::max();
    assert(f.capture(0).goal.remaining==0);
}
}

void placement_feedback_visibility() {
    Fixture f;auto& p=f.player;
    p.gameplay.scoring.placedBlocks=1;
    p.presentation.placement={1,Piece::Type::from_index(5),{{18,4},Runtime::Rotation{}},{19},-2};
    f.config.display.effects.shake=123;
    p.presentation.boardMotion={3,4,-1,2};
    auto view=f.capture(1);
    assert(view.spatialEffects && view.placement && view.placement->piece.pose.position.row==18);
    assert(view.motion.wallSequence==3 && view.motion.wallDirection==-1 && view.motion.rotationSequence==4);
    assert(view.placement->clearedRows==std::vector<int>{19} && view.placement->dropFromRow==-2 && view.effects.shake==123);
    f.config.field.invisible=true;view=f.capture(1);assert(!view.spatialEffects && !view.placement && view.motion.wallSequence==0 && view.motion.rotationSequence==0);
    f.config.mode.replayMode=true;view=f.capture(1);assert(!view.spatialEffects && !view.placement && view.motion.wallSequence==0 && view.motion.rotationSequence==0);
    f.config.field.invisible=false;
    for(const auto rule:{Runtime::ChallengeRules::Blind,Runtime::ChallengeRules::Audio,Runtime::ChallengeRules::Instinct}) {
        f.config.challengeRules=rule;view=f.capture(1);assert(!view.spatialEffects && !view.placement && view.motion.wallSequence==0 && view.motion.rotationSequence==0);
    }
    f.config.challengeRules=Runtime::ChallengeRules::None;
    p.presentation.remoteFieldVisibility=Runtime::FieldVisibility{true,false,{},{},{}};
    view=f.capture(1);assert(!view.spatialEffects && !view.placement && view.motion.wallSequence==0 && view.motion.rotationSequence==0);
    p.presentation.remoteFieldVisibility.reset();p.gameplay.scoring.placedBlocks=2;
    view=f.capture(1);assert(view.spatialEffects && !view.placement); // Never replay a stale lock payload.
}
int run(int argc,char** argv) {placement_feedback_visibility(); immutable_snapshot(); visibility_and_replay(); opacity_curves(); finished_invisible_field(); text_input_lifecycle();
    upper_ten_rows_and_markers();requested_hidden_region();complete_hud_projection(); goal_cues();     return 0;
}
} // namespace TetrTests::frontend_snapshot_test
