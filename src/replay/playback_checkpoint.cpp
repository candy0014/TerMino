#include "tetr/replay/playback_checkpoint.h"

#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Replay {
namespace {

void rebase_player_timers(
    Runtime::PlayerSessionState& player,
    Runtime::TimePoint now) {
    player.timing.timedGarbage=now;
    player.timing.frameUpdate=now;
    player.timing.framePoll=now;
    player.timing.autoplay=now;
    player.timing.qpGravity=now;
    player.timing.multiplayerSync=now;
}

Runtime::Duration next_checkpoint_after(Runtime::Duration elapsed) {
    const double interval=kPlaybackCheckpointIntervalSeconds;
    const double next=(std::floor(elapsed.seconds()/interval)+1.0)*interval;
    return Runtime::Duration::from_seconds(next);
}

} // namespace

void configure_playback_checkpoint_cache(
    Runtime::GameContext& context,
    const std::string& replayIdentity) {
    auto& state=session(context);
    state.pendingCheckpointRestore.reset();
    if(state.checkpointIdentity==replayIdentity) return;
    state.checkpointIdentity=replayIdentity;
    state.checkpoints.clear();
    state.nextCheckpointAt=Runtime::Duration::from_seconds(
        kPlaybackCheckpointIntervalSeconds);
}

void capture_playback_checkpoint(Runtime::GameContext& context) {
    if(!context.config.mode.replayMode) return;
    auto& state=session(context);
    if(!state.playback.started) return;
    // Checkpoints are captured only at safe between-piece boundaries. Their
    // state therefore corresponds to the last operation actually consumed,
    // not to the scheduling clock, which can run far ahead at high speed.
    const Runtime::Duration elapsed=state.lastConsumedTimestamp;
    const bool initial=state.checkpoints.empty();
    if(!initial&&elapsed<state.nextCheckpointAt) return;

    ReplaySessionState::PlaybackCheckpoint checkpoint;
    checkpoint.elapsed=elapsed;
    checkpoint.session=context.session;
    checkpoint.serviceRandom=context.services.random;
    checkpoint.remainingOperationCount=state.operations.size();
    checkpoint.gameElapsed=state.lastGameTimestamp;
    checkpoint.gameClockPaused=state.gameClockPaused;
    if (state.turnPlayback)
        checkpoint.completedPieces=state.turnPlayback->completedPieces[state.turnPlayerIndex];
    state.checkpoints.push_back(std::move(checkpoint));
    state.nextCheckpointAt=next_checkpoint_after(elapsed);
}

Runtime::Duration schedule_playback_checkpoint_restore(
    Runtime::GameContext& context,
    Runtime::Duration target) {
    auto& state=session(context);
    state.pendingCheckpointRestore.reset();
    for(std::size_t index=0;index<state.checkpoints.size();++index){
        if(state.checkpoints[index].elapsed>target) break;
        state.pendingCheckpointRestore=index;
    }
    if(!state.pendingCheckpointRestore) return {};
    return state.checkpoints[*state.pendingCheckpointRestore].elapsed;
}

bool restore_scheduled_playback_checkpoint(Runtime::GameContext& context) {
    auto& state=session(context);
    if(!state.pendingCheckpointRestore) return false;
    const std::size_t index=*state.pendingCheckpointRestore;
    state.pendingCheckpointRestore.reset();
    if(index>=state.checkpoints.size()) return false;

    const auto& checkpoint=state.checkpoints[index];
    if(state.operations.size()<checkpoint.remainingOperationCount) return false;
    while(state.operations.size()>checkpoint.remainingOperationCount){
        state.operations.pop();
    }
    context.session=checkpoint.session;
    if (Runtime::enabled(context.config.quickPlay.snowball)) {
        // The prefix has the initial size, while checkpoints retain the size
        // reached at this particular point in the recording.
        context.config.field.width=context.session.player.gameplay.playfield.width;
        context.config.field.height=context.session.player.gameplay.playfield.visibleHeight;
    }
    context.services.random=checkpoint.serviceRandom;
    state.lastConsumedTimestamp=checkpoint.elapsed;
    state.lastGameTimestamp=checkpoint.gameElapsed;
    state.gameClockPaused=checkpoint.gameClockPaused;
    if (state.turnPlayback)
        state.turnPlayback->completedPieces[state.turnPlayerIndex]=checkpoint.completedPieces;

    const Runtime::TimePoint now=context.now();
    context.session.gameStartTime=now;
    rebase_player_timers(context.session.player,now);
    rebase_player_timers(context.session.opponent,now);
    return true;
}

std::size_t playback_checkpoint_count(
    const Runtime::GameContext& context) noexcept {
    const auto* state=context.replay_component();
    return state?state->checkpoints.size():0;
}

} // namespace Replay
