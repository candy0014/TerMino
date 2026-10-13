#include "tetr/engine/undo.h"

#include "tetr/autoplay/strategy.h"
#include "tetr/input/services.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/playback_checkpoint.h"
#include "tetr/replay/tetrio_recorder.h"
#include "tetr/runtime/game_context.h"

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace {

// Keep every typed component, but store only occupied board cells rather than
// duplicating the playfield's two fixed 105x105 storage arrays at every lock.
struct PlayerSnapshot {
    struct Cell { int row, column; Runtime::BoardCell value; bool initial=false; };
    std::array<int,8> board{};
    bool holdEnabled=false, initialized=false;
    std::vector<Cell> cells;
    Runtime::ScoringState scoring;
    Runtime::HoldState hold;
    Runtime::QuickPlayPlacementState quickPlay;
    Runtime::PieceQueueState pieces;
    Runtime::GarbageSessionState garbage;
    std::mt19937 attackRandom;
    Runtime::MultiplayerSessionState multiplayer;
    Runtime::PlayerTimingState timing;
    Runtime::ClutchSessionState clutch;
    Runtime::PlayerPresentationState presentation;

    explicit PlayerSnapshot(const Runtime::PlayerSessionState& player)
        : scoring(player.gameplay.scoring), hold(player.gameplay.hold),
          quickPlay(player.gameplay.quickPlay), pieces(player.pieces), garbage(player.garbage),
          attackRandom(player.attackRoundingRandom), multiplayer(player.multiplayer),
          timing(player.timing), clutch(player.clutch), presentation(player.presentation) {
        const auto& field=player.gameplay.playfield;
        board={field.width,field.visibleHeight,field.activeHeight,field.nextCount,
            field.garbageHeight,field.solidGarbageHeight,field.field.width(),field.field.height()};
        holdEnabled=field.holdEnabled;
        initialized=field.field.initialized();
        if (initialized) for (int row=-Runtime::kHiddenRowCapacity; row<field.field.height(); ++row)
            for (int col=0; col<field.field.width(); ++col)
                if (field.field.cell(row,col).occupied()) cells.push_back({row,col,field.field.cell(row,col),field.field.initial_cell(row,col)});
    }

    void restore(Runtime::PlayerSessionState& player) const {
        auto& field=player.gameplay.playfield;
        field.width=board[0]; field.visibleHeight=board[1]; field.activeHeight=board[2];
        field.nextCount=board[3]; field.garbageHeight=board[4]; field.solidGarbageHeight=board[5];
        field.holdEnabled=holdEnabled;
        if (initialized) {
            field.field.initialize(board[6],board[7]);
            for (const auto& cell:cells) {
                field.field.set_cell(cell.row,cell.column,cell.value);
                if(cell.initial) field.field.mark_initial_cell(cell.row,cell.column);
            }
        } else field.field=Runtime::Playfield{};
        player.gameplay.scoring=scoring; player.gameplay.hold=hold;
        player.gameplay.quickPlay=quickPlay; player.pieces=pieces; player.garbage=garbage;
        player.attackRoundingRandom=attackRandom; player.multiplayer=multiplayer;
        player.timing=timing; player.clutch=clutch; player.presentation=presentation;
    }
};

struct ReplaySnapshot {
    std::size_t dataSize=0, remainingOperations=0;
    Runtime::Duration consumed{}, gameElapsed{};
    bool separateGameTime=false, gameClockPaused=false;
    std::optional<Runtime::TimePoint> recordingStart;
    Replay::ReplaySessionState::PlaybackState playback;
    std::optional<Replay::TetrioRecorder> tetrioRecorder;
};

struct UndoSnapshot {
    Runtime::RuntimeConfig config;
    PlayerSnapshot player, opponent;
    std::optional<Runtime::TimePoint> gameStartTime;
    Runtime::QuickPlaySessionState quickPlay;
    Runtime::DynamicRuleState dynamicRules;
    Runtime::TimePoint simulationTime;
    std::uint64_t completedPieces;
    Runtime::Duration turnWaitDuration, clockOffset;
    std::optional<Runtime::TimePoint> turnWaitStarted;
    std::mt19937 serviceRandom;
    Runtime::TimePoint realTime;
    Engine::PieceLoopState loop;
    std::any autoplay;
    std::optional<ReplaySnapshot> replay;

    explicit UndoSnapshot(const Runtime::GameContext& context)
        : config(context.config), player(context.session.player), opponent(context.session.opponent),
          gameStartTime(context.session.gameStartTime), quickPlay(context.session.quickPlay),
          dynamicRules(context.session.dynamicRules), simulationTime(context.session.simulationTime),
          completedPieces(context.session.completedSimulationPieces), turnWaitDuration(context.session.turnWaitDuration),
          clockOffset(context.session.clockOffset), turnWaitStarted(context.session.turnWaitStarted),
          serviceRandom(context.services.random), realTime(context.real_now()) {
        if (context.autoplay_component()) autoplay=context.autoplay_component()->capture_state();
        if (const auto* state=context.replay_component()) {
            replay.emplace();
            replay->dataSize=state->data.size(); replay->remainingOperations=state->operations.size();
            replay->consumed=state->lastConsumedTimestamp; replay->gameElapsed=state->lastGameTimestamp;
            replay->separateGameTime=state->separateGameTime; replay->gameClockPaused=state->gameClockPaused;
            replay->recordingStart=state->recordingStart; replay->playback=state->playback;
            if (context.config.mode.replayMode) {
                // Fast playback and seek rebuilds can schedule far ahead of
                // the board. Resume from this boundary's consumed event,
                // rather than immediately replaying the undone locks again.
                auto& playback=replay->playback;
                playback.elapsed=replay->consumed;
                playback.anchor=realTime;
                playback.progressElapsed=replay->consumed;
                if (playback.rebuilding) {
                    playback.paused=playback.resumePaused;
                    playback.resuming=false;
                    playback.rebuilding=false;
                    playback.rebuildComplete=true;
                }
            }
            if (state->tetrioRecorder) replay->tetrioRecorder=*state->tetrioRecorder;
        }
    }
};

void rebase_wall_timers(Runtime::PlayerSessionState& player, Runtime::Duration offset, bool replay) {
    player.timing.autoplay+=offset;
    player.timing.framePoll+=offset;
    player.timing.multiplayerSync+=offset;
    if (player.timing.entryDelayEnd) *player.timing.entryDelayEnd+=offset;
    if (replay) {
        player.timing.frameUpdate+=offset;
        player.timing.timedGarbage+=offset;
        player.timing.qpGravity+=offset;
    }
}

} // namespace

namespace Runtime {
struct UndoHistory {
    std::vector<UndoSnapshot> snapshots;
    std::vector<Replay::Operation> replayOperations;
    Engine::PieceLoopState* loop=nullptr;
    bool keyDown=false;
    std::uint64_t keyEventCursor=0;
    std::size_t pendingPresses=0;
};
} // namespace Runtime

namespace Engine {

bool undo_enabled(const Runtime::GameContext& context) noexcept {
    return !context.config.disableUndo &&
        context.config.mode.mode!=Runtime::GameMode::Multiplayer &&
        context.config.mode.mode!=Runtime::GameMode::LocalVersus &&
        context.config.garbage.mode!=Runtime::GarbageMode::Multiplayer &&
        (!context.replay_component() || !context.replay_component()->localVersusReplay);
}

void begin_undo_history(Runtime::GameContext& context, PieceLoopState* loopState) {
    context.undoHistory.reset();
    if (!undo_enabled(context)) return;
    context.undoHistory=std::make_shared<Runtime::UndoHistory>();
    auto& history=*context.undoHistory;
    history.loop=loopState;
    if (context.replay_component() && context.config.mode.replayMode) {
        auto operations=context.replay_component()->operations;
        history.replayOperations.reserve(operations.size());
        while (!operations.empty()) {
            history.replayOperations.push_back(operations.front()); operations.pop();
        }
    }
    if (context.input_component()) {
        // Ignore events and held keys that predate this game/history.
        context.input_services().read_control_events(history.keyEventCursor);
        for (const auto& [key,action]:context.input_services().bindings.entries())
            if (action==Input::Action::Undo) history.keyDown|=context.input_services().key_down(key);
    }
    capture_undo_snapshot(context);
}

void capture_undo_snapshot(Runtime::GameContext& context) {
    if (!context.undoHistory || !undo_enabled(context)) return;
    auto& history=*context.undoHistory;
    // Hold, time expiry without a lock, and spawn failure are not new locks.
    if (!history.snapshots.empty() &&
        history.snapshots.back().player.scoring.placedBlocks==context.session.player.gameplay.scoring.placedBlocks) return;
    history.snapshots.emplace_back(context);
    if (history.loop) history.snapshots.back().loop=*history.loop;
}

bool restore_previous_undo_snapshot(Runtime::GameContext& context) {
    if (!undo_enabled(context) || !context.undoHistory || context.undoHistory->snapshots.empty()) return false;
    auto& history=*context.undoHistory;
    // If interrupted during lock/clear settlement, the newest safe boundary
    // already precedes that lock. Otherwise remove the most recent completed lock.
    const bool pop=history.snapshots.size()>1 && context.session.player.gameplay.scoring.placedBlocks<=
        history.snapshots.back().player.scoring.placedBlocks;
    const auto& snapshot=history.snapshots[history.snapshots.size()-1-(pop?1:0)];
    if (snapshot.replay && context.config.mode.replayMode &&
        snapshot.replay->remainingOperations>history.replayOperations.size()) return false;
    if (pop) history.snapshots.pop_back();
    const Runtime::Duration wallOffset=context.real_now()-snapshot.realTime;
    context.config=snapshot.config;
    snapshot.player.restore(context.session.player); snapshot.opponent.restore(context.session.opponent);
    auto& session=context.session;
    session.gameStartTime=snapshot.gameStartTime; session.quickPlay=snapshot.quickPlay;
    session.dynamicRules=snapshot.dynamicRules; session.simulationTime=snapshot.simulationTime;
    session.completedSimulationPieces=snapshot.completedPieces; session.turnWaitDuration=snapshot.turnWaitDuration;
    session.turnWaitStarted=snapshot.turnWaitStarted; session.clockOffset=snapshot.clockOffset;
    if (!context.config.mode.replayMode && !context.uses_piece_time()) session.clockOffset+=wallOffset;
    if (context.config.mode.replayMode && session.gameStartTime) *session.gameStartTime+=wallOffset;
    rebase_wall_timers(session.player,wallOffset,context.config.mode.replayMode);
    rebase_wall_timers(session.opponent,wallOffset,context.config.mode.replayMode);
    context.services.random=snapshot.serviceRandom;
    if (history.loop) {
        *history.loop=snapshot.loop;
        history.loop->input.rebase_timers(wallOffset);
    }
    if (context.autoplay_component()) context.autoplay_component()->restore_state(snapshot.autoplay);
    if (snapshot.replay && context.replay_component()) {
        const auto& saved=*snapshot.replay;
        auto& state=*context.replay_component();
        const bool viewerPaused = state.playback.paused;
        if (!context.config.mode.replayMode) state.data.resize(saved.dataSize);
        else {
            state.operations={};
            for (std::size_t i=history.replayOperations.size()-saved.remainingOperations;
                i<history.replayOperations.size(); ++i) state.operations.push(history.replayOperations[i]);
        }
        state.lastConsumedTimestamp=saved.consumed; state.lastGameTimestamp=saved.gameElapsed;
        state.separateGameTime=saved.separateGameTime; state.gameClockPaused=saved.gameClockPaused;
        state.recordingStart=saved.recordingStart;
        if (state.recordingStart) *state.recordingStart+=wallOffset;
        state.playback=saved.playback;
        if (context.config.mode.replayMode && viewerPaused) state.playback.paused = true;
        state.playback.anchor+=wallOffset; state.playback.lastProgressRender+=wallOffset;
        if (saved.tetrioRecorder && state.tetrioRecorder) *state.tetrioRecorder=*saved.tetrioRecorder;
        state.pendingCheckpointRestore.reset();
        state.checkpoints.erase(std::remove_if(state.checkpoints.begin(),state.checkpoints.end(),
            [&](const auto& checkpoint) { return checkpoint.elapsed>saved.consumed; }),state.checkpoints.end());
        state.nextCheckpointAt=Runtime::Duration::from_seconds(
            (std::floor(saved.consumed.seconds()/Replay::kPlaybackCheckpointIntervalSeconds)+1)*Replay::kPlaybackCheckpointIntervalSeconds);
    }
    if (context.replay_component() && !context.config.mode.replayMode)
        ++context.replay_component()->undoCount;
    return true;
}

void poll_undo_key(Runtime::GameContext& context) {
    if (!context.undoHistory || !undo_enabled(context) || !context.input_component()) return;
    auto& history=*context.undoHistory;
    auto& input=context.input_services();
    for (const auto& event:input.read_control_events(history.keyEventCursor))
        if (event.down && input.bindings.action_for(event.key)==Input::Action::Undo) ++history.pendingPresses;
    bool down=false;
    for (const auto& [key,action]:context.input_services().bindings.entries())
        if (action==Input::Action::Undo) down|=context.input_services().key_down(key);
    const bool queued=history.pendingPresses>0;
    if (queued) --history.pendingPresses;
    const bool pressed=queued || (down && !history.keyDown);
    history.keyDown=down;
    if (pressed && !history.snapshots.empty()) throw UndoRequested{};
}

std::size_t undo_snapshot_count(const Runtime::GameContext& context) noexcept {
    return context.undoHistory ? context.undoHistory->snapshots.size() : 0;
}

} // namespace Engine
