#ifndef TETR_REPLAY_PLAYBACK_CLOCK_H
#define TETR_REPLAY_PLAYBACK_CLOCK_H

#include "tetr/runtime/time.h"

namespace Runtime { class GameContext; }

namespace Replay {
// Local rule time (possibly discrete) independent of the presentation clock.
Runtime::Duration playback_game_elapsed(
    const Runtime::GameContext& context, Runtime::TimePoint now) noexcept;

// Playback speeds are clamped to [1/1024, 1024] for start, resume and controls.
void configure_playback(
    Runtime::GameContext& context,
    double speed,
    Runtime::Duration duration);
void prepare_playback_resume(
    Runtime::GameContext& context,
    Runtime::Duration elapsed,
    double speed,
    bool paused,
    Runtime::Duration rebuildBaseElapsed = {});
void complete_playback_resume(
    Runtime::GameContext& context,
    Runtime::TimePoint now);
void start_playback(Runtime::GameContext& context, Runtime::TimePoint now);
// Called after both local streams start, before their decoding workers launch.
// Both contexts retain their own rule/board time while sharing viewer controls.
void share_playback_clock(Runtime::GameContext& first, Runtime::GameContext& second);
// A mode goal may end playback before trailing recorded markers are consumed.
// Latch the completed timeline without discarding events needed for Undo/seek.
void finish_playback(Runtime::GameContext& context, Runtime::TimePoint now);

// Elapsed time is expressed in the replay's original recorded timeline.  It
// advances by `speed` replay-seconds per wall-clock second and freezes while
// paused, so every replay consumer can share one clock.
Runtime::Duration playback_elapsed(
    const Runtime::GameContext& context,
    Runtime::TimePoint now) noexcept;
// Timestamp of the latest replay operation already consumed by the board.
// Unlike playback_elapsed(), this cannot run ahead when high-speed playback
// produces operations faster than the terminal can present them.
Runtime::Duration playback_presented_elapsed(
    const Runtime::GameContext& context) noexcept;
// The progress bar follows applied events while due operations are backlogged,
// retaining already presented silent time; it advances smoothly when caught up.
Runtime::Duration playback_progress_elapsed(
    Runtime::GameContext& context,
    Runtime::TimePoint now) noexcept;
Runtime::Duration playback_duration(const Runtime::GameContext& context) noexcept;
double playback_speed(const Runtime::GameContext& context) noexcept;
bool playback_paused(const Runtime::GameContext& context) noexcept;
bool playback_finished(const Runtime::GameContext& context) noexcept;
bool playback_resuming(const Runtime::GameContext& context) noexcept;
bool playback_rebuilding(const Runtime::GameContext& context) noexcept;

// Replay-only simulation clocks.  Frame presentation may still refresh on a
// wall-clock cadence, but gameplay deltas and periodic Quick Play changes must
// freeze while paused and scale with the replay timeline.
Runtime::Duration consume_playback_frame_delta(
    Runtime::GameContext& context,
    Runtime::TimePoint now) noexcept;
bool playback_qp_gravity_due(
    Runtime::GameContext& context,
    Runtime::TimePoint now,
    Runtime::Duration interval) noexcept;

void set_playback_speed(
    Runtime::GameContext& context,
    Runtime::TimePoint now,
    double speed);
void toggle_playback_pause(
    Runtime::GameContext& context,
    Runtime::TimePoint now);

} // namespace Replay

#endif
