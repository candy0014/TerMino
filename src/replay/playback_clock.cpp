#include "tetr/replay/playback_clock.h"

#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"

#include <algorithm>
#include <cmath>

namespace Replay {
namespace {

double valid_speed(double speed) noexcept {
    return std::isfinite(speed)&&speed>0.0
        ? std::clamp(speed,1.0/1024.0,1024.0) : 1.0;
}

Runtime::Duration current_elapsed(
    const ReplaySessionState::PlaybackState& playback,
    Runtime::TimePoint now) noexcept {
    if (playback.finished) return playback.duration;
    Runtime::Duration elapsed = playback.elapsed;
    if (playback.started && !playback.paused) {
        elapsed += (now - playback.anchor) * playback.speed;
    }
    if (playback.duration > Runtime::Duration{} && elapsed > playback.duration) {
        return playback.duration;
    }
    return elapsed;
}

void commit_elapsed(
    Runtime::GameContext& context,
    Runtime::TimePoint now) noexcept {
    auto& playback=session(context).playback;
    // Controls act on the visible board, not on unprocessed scheduled work.
    // Otherwise pausing/slowing a high-speed replay still drains the old backlog.
    // The two local-versus streams must retain their common scheduling time;
    // rebasing each to its own consumed marker would desynchronize their turns.
    playback.elapsed = session(context).localVersusReplay
        ? current_elapsed(playback,now) : playback_progress_elapsed(context,now);
    playback.anchor = now;
}

} // namespace

void configure_playback(
    Runtime::GameContext& context,
    double speed,
    Runtime::Duration duration) {
    auto& playback=session(context).playback;
    playback={};
    playback.speed=valid_speed(speed);
    playback.duration=duration;
}

void prepare_playback_resume(
    Runtime::GameContext& context,
    Runtime::Duration elapsed,
    double speed,
    bool paused,
    Runtime::Duration rebuildBaseElapsed) {
    auto& playback=session(context).playback;
    playback.elapsed=std::max(elapsed,Runtime::Duration{});
    playback.speed=valid_speed(speed);
    playback.rebuildBaseElapsed=std::max(
        Runtime::Duration{},std::min(rebuildBaseElapsed,playback.elapsed));
    // Keep the timeline fixed exactly at the requested timestamp while the
    // operation stream is rebuilt. The user's prior pause state is restored
    // only after the final frame has been drawn.
    playback.paused=true;
    playback.resuming=true;
    playback.resumePaused=paused;
    playback.rebuilding=true;
    playback.rebuildComplete=false;
    playback.finished=false;
    context.config.timeMultiplier=playback.speed;
}

void complete_playback_resume(
    Runtime::GameContext& context,
    Runtime::TimePoint now) {
    auto& playback=session(context).playback;
    playback.anchor=now;
    playback.paused=playback.resumePaused;
    playback.resuming=false;
    playback.rebuilding=false;
    playback.rebuildComplete=true;
}

void start_playback(Runtime::GameContext& context, Runtime::TimePoint now) {
    auto& state=session(context);
    auto& playback=state.playback;
    const Runtime::Duration initialElapsed=playback.resuming
        ? std::min(playback.elapsed,playback.duration)
        : Runtime::Duration{};
    const bool initiallyPaused=playback.resuming;
    playback.elapsed=initialElapsed;
    const Runtime::Duration simulationBase=playback.resuming
        ? playback.rebuildBaseElapsed
        : Runtime::Duration{};
    playback.lastFrameRuleUpdate=state.separateGameTime ? state.lastGameTimestamp : simulationBase;
    playback.lastQpGravityUpdate=playback.lastFrameRuleUpdate;
    playback.anchor=now;
    playback.lastProgressRender=now;
    playback.progressElapsed=state.lastConsumedTimestamp;
    playback.started=true;
    playback.finished=false;
    playback.paused=initiallyPaused;
    playback.progressRendered=false;
    playback.controlsInitialized=false;
    playback.pauseKeyDown=false;
    playback.slowerKeyDown=false;
    playback.fasterKeyDown=false;
    playback.backwardKeyDown=false;
    playback.forwardKeyDown=false;
}

void finish_playback(Runtime::GameContext& context, Runtime::TimePoint now) {
    auto& playback=session(context).playback;
    playback.finished=true;
    playback.elapsed=playback.duration;
    playback.progressElapsed=playback.duration;
    playback.anchor=now;
    playback.progressRendered=false;
}

Runtime::Duration playback_elapsed(
    const Runtime::GameContext& context,
    Runtime::TimePoint now) noexcept {
    const auto* state=context.replay_component();
    if (!state) return {};
    return current_elapsed(state->playback,now);
}

Runtime::Duration playback_presented_elapsed(
    const Runtime::GameContext& context) noexcept {
    const auto* state=context.replay_component();
    if (!state) return {};
    return std::max(
        Runtime::Duration{},
        std::min(state->lastConsumedTimestamp,state->playback.duration));
}

Runtime::Duration playback_game_elapsed(
    const Runtime::GameContext& context, Runtime::TimePoint now) noexcept {
    const auto* state = context.replay_component();
    if (!state) return {};
    const auto scheduled=playback_elapsed(context,now);
    const bool backlogged=context.config.garbage.mode==Runtime::GarbageMode::QuickPlay &&
        !state->operations.empty() && state->operations.front().timestamp<=scheduled;
    // Rule state must follow applied events while decoding catches up. Running
    // protection/fatigue ahead changes the RNG-derived QP garbage holes and
    // makes later recorded landings collide with a different board.
    if (!state->separateGameTime)
        return backlogged ? state->lastGameTimestamp : scheduled;
    auto elapsed = state->lastGameTimestamp;
    if (context.config.simulationPps == 0 && !state->gameClockPaused && !state->operations.empty() && !backlogged)
        elapsed += std::max(Runtime::Duration{},
            playback_elapsed(context, now) - state->lastConsumedTimestamp);
    return elapsed;
}

Runtime::Duration playback_progress_elapsed(
    Runtime::GameContext& context,
    Runtime::TimePoint now) noexcept {
    auto* state=context.replay_component();
    if (!state) return {};
    if (state->playback.finished) return state->playback.duration;
    const Runtime::Duration scheduled=current_elapsed(state->playback,now);
    const bool backlogged=!state->operations.empty()&&
        state->operations.front().timestamp<=scheduled;
    // Do not extrapolate a second UI clock: it can both lead the board after an
    // expensive frame and lag behind bursts of quickly decoded operations.
    // Silent intervals still advance smoothly once all due events are applied.
    // A previously displayed quiet interval remains valid even if its first
    // event is now overdue. Keep that position until applied events pass it.
    auto& progress=state->playback.progressElapsed;
    progress=backlogged ? std::max(progress,playback_presented_elapsed(context)) : scheduled;
    return progress;
}

Runtime::Duration playback_duration(const Runtime::GameContext& context) noexcept {
    const auto* state=context.replay_component();
    return state ? state->playback.duration : Runtime::Duration{};
}

double playback_speed(const Runtime::GameContext& context) noexcept {
    const auto* state=context.replay_component();
    return state ? state->playback.speed : 1.0;
}

bool playback_paused(const Runtime::GameContext& context) noexcept {
    const auto* state=context.replay_component();
    return state && state->playback.paused;
}

bool playback_finished(const Runtime::GameContext& context) noexcept {
    const auto* state=context.replay_component();
    return state && state->playback.finished;
}

bool playback_resuming(const Runtime::GameContext& context) noexcept {
    const auto* state=context.replay_component();
    return state&&state->playback.resuming;
}

bool playback_rebuilding(const Runtime::GameContext& context) noexcept {
    const auto* state=context.replay_component();
    return state&&state->playback.rebuilding;
}

Runtime::Duration consume_playback_frame_delta(
    Runtime::GameContext& context,
    Runtime::TimePoint now) noexcept {
    auto* state=context.replay_component();
    if (!state) return {};
    auto& playback=state->playback;
    const Runtime::Duration elapsed=playback_game_elapsed(context,now);
    Runtime::Duration delta=elapsed-playback.lastFrameRuleUpdate;
    if (delta<Runtime::Duration{}) delta={};
    playback.lastFrameRuleUpdate=elapsed;
    return delta;
}

bool playback_qp_gravity_due(
    Runtime::GameContext& context,
    Runtime::TimePoint now,
    Runtime::Duration interval) noexcept {
    auto* state=context.replay_component();
    if (!state) return false;
    auto& playback=state->playback;
    const Runtime::Duration elapsed=playback_game_elapsed(context,now);
    if (elapsed-playback.lastQpGravityUpdate<=interval) return false;
    playback.lastQpGravityUpdate=elapsed;
    return true;
}

void set_playback_speed(
    Runtime::GameContext& context,
    Runtime::TimePoint now,
    double speed) {
    auto& playback=session(context).playback;
    commit_elapsed(context,now);
    playback.speed=valid_speed(speed);
    context.config.timeMultiplier=playback.speed;
}

void toggle_playback_pause(
    Runtime::GameContext& context,
    Runtime::TimePoint now) {
    auto& playback=session(context).playback;
    commit_elapsed(context,now);
    playback.paused=!playback.paused;
}

} // namespace Replay
