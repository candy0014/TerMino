#include "tetr/engine/locked_piece_resolution.h"

#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/piece.h"
#include <algorithm>

namespace Engine {

Rules::PlacementResult resolve_locked_piece(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    Piece::Pose pose,
    Runtime::RotationRecord lastRotation,
    Runtime::Duration lockedAt) {
    auto& playfield = player.gameplay.playfield;
    auto& scoring = player.gameplay.scoring;

    const bool grounded = !piece.can_place(pose.translated(1, 0), field);
    bool immobile = true;
    static constexpr int directions[4][2] = {{1,0},{0,1},{-1,0},{0,-1}};
    for (const auto& direction : directions) {
        if (piece.can_place(pose.translated(direction[0], direction[1]), field)) {
            immobile = false;
            break;
        }
    }
    const bool trackLockTime=context.config.field.invisible && !context.config.field.periodicInvisible &&
        context.config.field.invisibilityDelay>Runtime::Duration{};
    for (const auto& cell : piece.cells(pose.rotation)) {
        field.set_cell(
            pose.position.row + cell[0],
            pose.position.col + cell[1],
            Runtime::BoardCell::piece(piece.color_id()));
        if(trackLockTime) field.mark_cell_lock_time(pose.position.row+cell[0],pose.position.col+cell[1],lockedAt);
    }

    const Piece::Metadata pieceMetadata = Piece::metadata(piece.type());
    Rules::PlacementInput input;
    input.clearBatch=context.config.challengeRules==Runtime::ChallengeRules::Stack?4:0;
    input.retainFullRows=context.config.challengeRules==Runtime::ChallengeRules::ConstructMap;
    input.attackRules = context.config.attackRules;
    input.mapWidth = context.config.field.width;
    input.mapHeight = playfield.activeHeight;
    input.firstRow = -Runtime::kHiddenRowCapacity;
    input.garbageStartRow = playfield.activeHeight - playfield.garbageHeight;
    input.layerStartRow = playfield.activeHeight - context.config.garbage.layerHeight;
    input.pose = pose;
    input.lastRotation = lastRotation;
    input.immobile = immobile;
    input.grounded = grounded;
    input.spinChecker = pieceMetadata.spinCheck;
    input.canonicalGeometry = pieceMetadata.tetrioRotation;
    input.spinBonuses = context.config.spinBonuses;
    input.garbageMultiplier = context.config.garbage.sendMultiplier;
    input.legacySpinScoring = context.config.legacySpinScoring;
    if (Runtime::reverse(context.config.quickPlay.doubleHole) &&
        pieceMetadata.spinCheck != Piece::SpinCheck::TCorner &&
        pieceMetadata.spinCheck != Piece::SpinCheck::ArsTCorner)
        input.lastRotation = Runtime::RotationRecord{};
    input.currentB2B = scoring.b2b;
    input.currentCombo = scoring.combo;
    if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay) {
        input.attackRoundingRoll = static_cast<double>(player.attackRoundingRandom()) /
            (static_cast<double>(std::mt19937::max()) + 1.0);
    }
    input.garbageMode = context.config.garbage.mode;
    input.allSpinMod = context.config.quickPlay.allSpin;
    input.noHoldMod = context.config.quickPlay.noHold;
    input.piece = {pieceMetadata.familyIndex, pieceMetadata.rotationSystem};
    input.qp = {
        context.config.quickPlay.expert,
        context.config.quickPlay.noHold,
        context.config.quickPlay.messy,
        context.config.quickPlay.gravity,
        context.config.quickPlay.volatileMode,
        context.config.quickPlay.doubleHole,
        context.config.quickPlay.invisible,
        context.config.quickPlay.allSpin,
        context.config.quickPlay.backfire,
        context.config.quickPlay.snakeEyes,
        context.config.quickPlay.snowball
    };

    auto result=Rules::resolve_placement(
        input,
        [&field](int row, int col) { return field.cell(row, col); },
        [&field](int row, int col) { return field.cell(row, col).occupied(); });
    if(context.config.initialMap && context.config.mode.mapGoal==Runtime::MapGoal::AllClear && result.clear.clearedLines>0) {
        bool empty=true;
        for(int row=-Runtime::kHiddenRowCapacity;row<playfield.activeHeight && empty;++row) {
            if(std::find(result.clear.clearedRows.begin(),result.clear.clearedRows.end(),row)!=result.clear.clearedRows.end()) continue;
            for(int column=0;column<field.width();++column) if(field.cell(row,column).occupied()) {empty=false;break;}
        }
        scoring.mapAllClearReached=scoring.mapAllClearReached || empty;
    }
    return result;
}

} // namespace Engine
