#include "tetr/engine/garbage_service.h"
#include "tetr/util/deterministic_random.h"

#include "tetr/engine/garbage_field.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/playback_clock.h"
#include "garbage_rendering.h"
#include "tetr/rules/garbage_generation.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/rules/quick_play.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"

#include <algorithm>
#include <vector>

namespace Engine::GarbageService {
namespace {

Rules::GarbageGenerationState generation_state(
    const Runtime::PlayerSessionState& player) {
    Rules::GarbageGenerationState state;
    const auto& storage = player.garbage;
    state.holeOrder = storage.holeOrder;
    state.holePattern = storage.holePattern;
    state.chessboardParity = storage.chessboardParity;
    state.lastColumn = storage.previousHoleColumn;
    return state;
}

void store_generation_state(
    Runtime::PlayerSessionState& player,
    const Rules::GarbageGenerationState& state) {
    player.garbage.holeOrder = state.holeOrder;
    player.garbage.holePattern = state.holePattern;
    player.garbage.chessboardParity = state.chessboardParity;
    player.garbage.previousHoleColumn = state.lastColumn;
}


} // namespace

void initialize(Runtime::PlayerSessionState& player, Runtime::GarbageHolePattern initialHolePattern) {
    player.garbage.previousHoleColumn = -1;
    auto& storage = player.garbage;
    player.presentation.garbageBufferDisplay.assign(
        static_cast<std::size_t>(std::max(0, player.gameplay.playfield.visibleHeight + Runtime::kDisplayHiddenRows + 1)), 0);
    player.presentation.garbageBufferSegmentTops.assign(player.presentation.garbageBufferDisplay.size(),false);
    storage.holePattern = initialHolePattern;
    storage.chessboardParity = false;
    Rules::GarbageGenerationState state;
    state.holeOrder.resize(player.gameplay.playfield.width);
    for (int column = 0; column < player.gameplay.playfield.width; ++column) {
        state.holeOrder[column] = column;
    }
    if (!storage.tetrioRandom) Util::deterministic_shuffle(state.holeOrder, storage.random);
    storage.holeOrder = state.holeOrder;
    player.gameplay.playfield.garbageHeight = 0;
    player.gameplay.playfield.solidGarbageHeight = 0;
    storage.buffer.clear();
    storage.rising.clear();
    storage.incomingFraction=0;
    storage.cancellation={};
    player.presentation.garbageBufferDisplayDeferred=false;
}

void add_garbage(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::Playfield& field,
    int height,
    bool preserveHole,
    Runtime::BoardCell cell,
    bool renderField,std::optional<double> qpElapsedSeconds) {
    // Never generate more cheese than a finite digging goal needs. Replays
    // keep their recorded rise events, including historical replenishment.
    if (!context.config.mode.replayMode && Runtime::is_cheese_mode(context.config.garbage.mode) &&
        Runtime::active_goal(context.config)==Runtime::GameGoal::Garbage) {
        const auto& gameplay=player.gameplay;
        const auto needed=std::max<std::int64_t>(0,static_cast<std::int64_t>(context.config.mode.garbageTarget)-
            gameplay.scoring.totalGarbageLines-gameplay.playfield.garbageHeight);
        height=static_cast<int>(std::min<std::int64_t>(std::max(0,height),needed));
        if(!height)return;
    }
    if (!context.config.mode.replayMode) {
        Replay::record_add_garbage(context, height, preserveHole, cell);
    }

    auto& playfield = player.gameplay.playfield;
    const auto before=renderField ? GarbageRendering::capture_field(context,player,field)
        : GarbageRendering::FieldImage{};
    height = std::min(height, playfield.activeHeight + Runtime::kHiddenRowCapacity);
    if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay &&
        context.config.quickPlay.continuousCancellation && height > 0)
        Rules::qp_relieve_cancellation(context.session.quickPlay.progress, height * 3);
    Rules::GarbageGenerationState state = generation_state(player);
    Rules::GarbageGenerationConfig generationConfig{
        playfield.width,
        context.config.garbage.mode,
        context.config.garbage.cheeseModel,
        context.config.garbage.cheeseMessiness,
        context.config.garbage.singleMessiness,
        context.config.quickPlay.doubleHole,
        cell
    };
    generationConfig.avoidSameColumn = context.config.garbage.avoidSameColumn;
    generationConfig.legacyShapeRules = context.config.garbage.legacyShapeRules;
    generationConfig.reverseNoHold = Runtime::reverse(context.config.quickPlay.noHold);
    if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay) {
        const auto& qp = context.session.quickPlay;
        generationConfig.qpPolicy = true;
        generationConfig.singleMessiness = Rules::qp_messiness(context.config.quickPlay,qp.floor,qp.progress,
            qpElapsedSeconds.value_or((context.config.mode.replayMode ? Replay::playback_game_elapsed(context,context.now()) :
                Runtime::elapsed_game_time(context.session,context.now())).seconds()));
        generationConfig.continuousMessiness = 2.5 * generationConfig.singleMessiness;
        generationConfig.favor = Rules::qp_garbage_favor(context.config.quickPlay,qp.floor);
        generationConfig.legacyQuickPlayRules=context.config.quickPlay.legacyRules;
        generationConfig.concentrate=Rules::qp_garbage_concentrates(context.config.quickPlay,qp.floor,qp.progress,
            qpElapsedSeconds.value_or((context.config.mode.replayMode ? Replay::playback_game_elapsed(context,context.now()) :
                Runtime::elapsed_game_time(context.session,context.now())).seconds()));
        generationConfig.digScores=Rules::qp_garbage_dig_scores(playfield.width,-Runtime::kHiddenRowCapacity,
            playfield.activeHeight,[&](int row,int column){return field.cell(row,column);},context.config.quickPlay.legacyRules);
    }
    playfield.garbageHeight += height;

    shift_field_up_for_garbage(
        playfield.width,
        -Runtime::kHiddenRowCapacity,
        playfield.activeHeight,
        height,
        [&field](int row, int column) { return field.cell(row, column); },
        [&field](int destination, int source) { field.copy_row(destination, source); },false);

    const bool interoperableHoles = player.garbage.tetrioRandom &&
        (generationConfig.legacyShapeRules ||
         (std::abs(generationConfig.cheeseModel)==1 && state.holePattern==Runtime::GarbageHolePattern::scattered(1) &&
          !Runtime::enabled(generationConfig.doubleHoleMode) && !generationConfig.reverseNoHold));
    if (!interoperableHoles) Rules::prepare_garbage_batch(
        generationConfig, state, player.garbage.random, preserveHole);

    for (int row = playfield.activeHeight - height; row < playfield.activeHeight; ++row) {
        std::vector<Runtime::BoardCell> values;
        if (interoperableHoles) {
            values.assign(playfield.width, cell);
            values[player.garbage.tetrioRandom->row(playfield.width,
                context.config.garbage.singleMessiness, context.config.garbage.avoidSameColumn)] = Runtime::BoardCell::empty();
        } else values = Rules::generate_garbage_row(
            generationConfig, state, player.garbage.random);
        write_generated_row(
            row,
            values,
            [&field](int r, int column) { return field.cell(r, column); },
            [&field](int r, int column, Runtime::BoardCell value) { field.set_cell(r,column,value); },false);
    }
    store_generation_state(player, state);
    if (renderField) GarbageRendering::render_field_changes(context,player,field,before);
}

void add_solid_garbage(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::Playfield& field,
    int height,
    Runtime::BoardCell cell) {
    if (!context.config.mode.replayMode) {
        Replay::record_add_solid_garbage(context, height, cell);
    }

    auto& playfield = player.gameplay.playfield;
    const auto paintedPose=player.presentation.paintedActivePose;
    const auto paintedType=player.presentation.paintedActivePieceType;
    const auto paintedPiece=paintedPose && paintedType
        ? std::optional<Piece::Definition>{Piece::get_definition(*paintedType)} : std::nullopt;
    const auto before=GarbageRendering::capture_field(context,player,field,
        paintedPiece ? &*paintedPiece : nullptr,paintedPose);
    height = std::min(height, playfield.activeHeight);
    int bottom = playfield.visibleHeight;
    if (cell.kind() == Runtime::BoardCellKind::TimedSolid) {
        bottom -= playfield.solidGarbageHeight;
    }

    shift_field_up_for_garbage(
        playfield.width,
        -Runtime::kHiddenRowCapacity,
        bottom,
        height,
        [&field](int row, int column) { return field.cell(row, column); },
        [&field](int destination, int source) { field.copy_row(destination, source); },false);

    Rules::GarbageGenerationState state = generation_state(player);
    for (int row = bottom - height; row < bottom; ++row) {
        const std::vector<Runtime::BoardCell> values = Rules::generate_solid_garbage_row(
            playfield.width,
            cell,
            context.config.quickPlay.doubleHole,
            5 + context.session.quickPlay.floor,
            state,
            player.garbage.random);
        write_generated_row(
            row,
            values,
            [&field](int r, int column) { return field.cell(r, column); },
            [&field](int r, int column, Runtime::BoardCell value) { field.set_cell(r,column,value); },false);
    }
    store_generation_state(player, state);

    playfield.activeHeight -= height;
    if (cell.kind() == Runtime::BoardCellKind::SolidGarbage) playfield.solidGarbageHeight += height;
    GarbageRendering::render_field_changes(context,player,field,before,
        paintedPiece ? &*paintedPiece : nullptr,paintedPose);
}

} // namespace Engine::GarbageService
