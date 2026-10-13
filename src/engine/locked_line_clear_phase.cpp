#include "tetr/engine/locked_line_clear_phase.h"
#include "tetr/engine/input_precharge.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/replay/session_state.h"

#include "tetr/engine/field_mutation.h"
#include "tetr/engine/line_clear_presentation.h"
#include "tetr/engine/replay_application.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/line_clear_event.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/turn_timeline.h"
#include "tetr/rules/line_clear.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"

#include <cstdio>
#include <algorithm>
#include <vector>

namespace Engine {

Rules::PlayResult run_locked_line_clear_phase(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    const Rules::PlacementResult& placement,
    const std::function<Rules::PlayResult()>& pumpFrame, MovementInputState* movementInput) {
    if (movementInput) precharge_movement_input(context,*movementInput);
    auto& playfield = player.gameplay.playfield;
    auto clearPositions=placement.clear.clearedRows;
    std::vector<bool> clearedRows(
        static_cast<std::size_t>(playfield.activeHeight + Runtime::kHiddenRowCapacity), false);
    for (int row : clearPositions) {
        const int index = row + Runtime::kHiddenRowCapacity;
        if (index >= 0 && index < static_cast<int>(clearedRows.size())) {
            clearedRows[static_cast<std::size_t>(index)] = true;
        }
    }
    const auto shift_clear_positions=[&](int risen) {
        if (risen<=0) return;
        std::fill(clearedRows.begin(),clearedRows.end(),false);
        for (auto& row:clearPositions) row-=risen;
        clearPositions.erase(std::remove_if(clearPositions.begin(),clearPositions.end(),
            [](int row) {return row < -Runtime::kHiddenRowCapacity;}),clearPositions.end());
        for (int row:clearPositions) clearedRows[static_cast<std::size_t>(row+Runtime::kHiddenRowCapacity)]=true;
    };

    LineClearPresentationCallbacks callbacks;
    callbacks.now = [&context]() {
        if (!context.config.mode.replayMode) return context.real_now();
        return Runtime::TimePoint::from_seconds(
            Replay::playback_elapsed(context,context.now()).seconds());
    };
    if (Replay::session(context).separateGameTime) {
        callbacks.animationStart = [&context]() {
            // Use the recorded clear marker even when rendering is catching up.
            // Starting at the capped playback clock can otherwise put the end
            // of the final animation beyond the end of the recording.
            return Runtime::TimePoint::from_seconds(
                Replay::session(context).lastConsumedTimestamp.seconds());
        };
    }
    callbacks.clearColumn = [&](int column) {
        Render::OutputGuard output(context);
        for (int row = -Runtime::kHiddenRowCapacity; row < playfield.activeHeight; ++row) {
            const int index = row + Runtime::kHiddenRowCapacity;
            if (index < 0 || index >= static_cast<int>(clearedRows.size()) ||
                !clearedRows[static_cast<std::size_t>(index)]) {
                continue;
            }
            field.set_cell(row,column,Runtime::BoardCell::empty());
            if (row >= -Runtime::kDisplayHiddenRows) {
                Render::draw_empty_board_cell(context,player,row,column);
            }
        }
    };
    callbacks.pumpFrame = [&]() -> Rules::PlayResult {
        const int beforeHeight=playfield.activeHeight;
        const auto signal=context.real_now()-player.timing.framePoll>Runtime::Duration::from_seconds(0.05)
            ? pumpFrame() : Rules::PlayResult::ContinueFrame;
        shift_clear_positions(beforeHeight-playfield.activeHeight);
        if (signal==Rules::PlayResult::ContinueFrame && !context.config.mode.replayMode &&
            Runtime::uses_garbage_buffer(context.config.garbage.mode)) {
            const int risen=context.config.garbage.entryMode==Runtime::GarbageEntryMode::Immediate
                ? GarbageService::raise_ready_buffer(context,player,field)
                : GarbageService::advance_garbage_rise(context,player,field);
            shift_clear_positions(risen);
        }
        return signal;
    };
    callbacks.pumpReplay = [&](LineClearPresentationMarker marker, bool leaveMarkerQueued) {
        const auto replayMarker = marker == LineClearPresentationMarker::ClearLine
            ? Replay::LineClearMarker::ClearLine
            : Replay::LineClearMarker::EndClearLine;
        const auto pump = Replay::pump_due_until_line_clear_marker(
            Replay::session(context).operations,
            Replay::playback_elapsed(context,context.now()),
            replayMarker,
            leaveMarkerQueued);
        if (pump.endAll) {
            return LineClearReplayPumpOutcome{Rules::PlayResult::Dead, false};
        }
        for (const auto& operation : pump.events) {
            Replay::mark_operation_consumed(context, operation);
            const int before=playfield.garbageHeight;
            const int beforeHeight=playfield.activeHeight;
            apply_replay_gameplay_event(context, operation, field, player, piece);
            shift_clear_positions(playfield.garbageHeight-before+beforeHeight-playfield.activeHeight);
        }
        return LineClearReplayPumpOutcome{Rules::PlayResult::ContinueFrame, pump.markerReached};
    };
    callbacks.recordReplayMarker = [&context](LineClearPresentationMarker marker) {
        Replay::add_replay(
            context,
            marker == LineClearPresentationMarker::ClearLine
                ? Replay::OperationKind::ClearLine
                : Replay::OperationKind::EndClearLine);
    };
    callbacks.flush = [&context]() {
        Render::OutputGuard output(context);
        std::fflush(stdout);
    };
    callbacks.idleWait = [&context,&player,movementInput]() {
        if (movementInput) precharge_movement_input(context,*movementInput);
        if (context.config.mode.replayMode) {
            update_replay_playback_controls(context,player);
        }
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    };

    const auto lineClearResult = run_line_clear_presentation(
        context.config.field.width,
        placement.clear.clearedLines,
        Replay::playback_rebuilding(context)
            ? Runtime::Duration{}
            : context.config.field.lineClearDelay,
        context.config.mode.replayMode,
        callbacks);
    if (lineClearResult != Rules::PlayResult::ContinueFrame) return lineClearResult;
    if (movementInput) precharge_movement_input(context,*movementInput);

    if (context.config.field.antiGravity) {
        const auto changes = apply_anti_gravity_clear(
            context.config.field.width,
            -Runtime::kDisplayHiddenRows,
            clearPositions,
            [&field](int row, int col) { return field.cell(row, col); },
            [&field](int row, int col, Runtime::BoardCell value) { field.set_cell(row,col,value); });
        Render::OutputGuard output(context);
        for (const auto& change : changes) {
            Render::draw_empty_board_cell(context,player,change.row,change.col);
        }
    } else {
        const auto collapsePlan = Rules::build_gravity_collapse_plan(
            -Runtime::kHiddenRowCapacity,
            playfield.activeHeight,
            clearPositions);
        const auto changes = apply_gravity_collapse(
            context.config.field.width,
            collapsePlan,
            [&field](int row, int col) { return field.cell(row, col); },
            [&field](int destination, int source) { field.copy_row(destination, source); });
        Render::OutputGuard output(context);
        for (const auto& change : changes) {
            if (change.row < -Runtime::kDisplayHiddenRows) continue;
            Render::draw_board_cell(context,player,change.row,change.col,change.value);
        }
    }
    if(context.config.mode.mode==Runtime::GameMode::Racing && context.config.initialMap && context.config.mode.mapGoal!=Runtime::MapGoal::Disabled) {
        Render::print_message(context,player);
        Render::flush();
    }
    return Rules::PlayResult::ContinueFrame;
}

} // namespace Engine
