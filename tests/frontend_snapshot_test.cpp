#include "tetr/frontend/text_input.h"
#include "tetr/presentation/board_view.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/runtime_config.h"
#include <cassert>
#include <memory>
#include <stdexcept>

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
    assert(view.next == originalNext && view.hold == f.player.gameplay.hold.piece && view.holdEnabled);
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
    assert(f.capture(4).at(19, 0).appearance == Presentation::CellAppearance::Visible);
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
    assert(view.goal==Runtime::GameGoal::Pieces && view.goalTarget==50 && view.goalProgress==12);
    assert(view.garbage==std::vector<int>{4} && view.wins==3);
    assert(view.spinPiece.valid() && view.mini && view.clearLines==2 && view.perfectClear);
    assert(view.spinHistory.size()==1 && view.at(0,4).hint);
    p.gameplay.quickPlay.operations.clear();assert(view.spinHistory.size()==1);
    f.config.challengeRules=Runtime::ChallengeRules::StageMaster;p.gameplay.scoring.challengeStages=2;
    assert(f.capture(2).stageTask=="2 SPINS");
}
}

int main() { immutable_snapshot(); visibility_and_replay(); text_input_lifecycle(); complete_hud_projection(); }
