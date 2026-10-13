#ifndef TETR_REPLAY_PLAYBACK_CHECKPOINT_H
#define TETR_REPLAY_PLAYBACK_CHECKPOINT_H

#include "tetr/runtime/time.h"

#include <cstddef>
#include <string>

namespace Runtime { class GameContext; }

namespace Replay {

inline constexpr double kPlaybackCheckpointIntervalSeconds=10.0;

// Checkpoints are process-local snapshots at safe between-piece boundaries.
// They are keyed by replay content and survive the reinitialization used by a
// seek, but are discarded automatically when another replay is loaded.
void configure_playback_checkpoint_cache(
    Runtime::GameContext& context,
    const std::string& replayIdentity);
void capture_playback_checkpoint(Runtime::GameContext& context);

// Selects the latest checkpoint not after target and returns its timestamp.
// restore_scheduled_playback_checkpoint() is called after normal board setup.
Runtime::Duration schedule_playback_checkpoint_restore(
    Runtime::GameContext& context,
    Runtime::Duration target);
bool restore_scheduled_playback_checkpoint(Runtime::GameContext& context);

std::size_t playback_checkpoint_count(
    const Runtime::GameContext& context) noexcept;

} // namespace Replay

#endif
