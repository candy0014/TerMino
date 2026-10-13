#ifndef TETR_REPLAY_SESSION_STATE_H
#define TETR_REPLAY_SESSION_STATE_H

#include "tetr/replay/operation.h"
#include "tetr/runtime/game_context.h"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace Replay {
class TetrioRecorder;

struct TurnPlaybackState {
    double quotas[2]{};
    std::atomic<std::uint64_t> completedPieces[2]{};
    std::atomic<bool> ended[2]{};
};

struct ReplaySessionState {
    std::shared_ptr<TetrioRecorder> tetrioRecorder;
    struct PlaybackCheckpoint {
        Runtime::Duration elapsed{};
        Runtime::SessionState session;
        std::mt19937 serviceRandom;
        std::size_t remainingOperationCount = 0;
        Runtime::Duration gameElapsed{};
        bool gameClockPaused = false;
        std::uint64_t completedPieces = 0;
    };

    struct PlaybackState {
        Runtime::Duration elapsed{};
        Runtime::Duration duration{};
        Runtime::Duration lastFrameRuleUpdate{};
        Runtime::Duration lastQpGravityUpdate{};
        Runtime::Duration rebuildBaseElapsed{};
        Runtime::Duration progressElapsed{};
        Runtime::TimePoint anchor{};
        Runtime::TimePoint lastProgressRender{};
        double speed = 1.0;
        int progressRow = -1;
        std::size_t lastProgressWidth = 0;
        std::size_t lastProgressRows = 0;
        bool progressVisible = true;
        bool started = false;
        bool finished = false;
        bool paused = false;
        bool progressRendered = false;
        bool controlsInitialized = false;
        bool pauseKeyDown = false;
        bool slowerKeyDown = false;
        bool fasterKeyDown = false;
        bool backwardKeyDown = false;
        bool forwardKeyDown = false;
        bool resuming = false;
        bool resumePaused = false;
        bool rebuilding = false;
        bool rebuildComplete = false;
    };

    std::string data;
    std::string configPrefix;
    // Recording metadata for the whole attempt; undo must never rewind it.
    std::uint64_t undoCount = 0;
    std::uint64_t pauseCount = 0;
    // Original round statistics, independent of viewer speed and post-game
    // waits. Optional so historical recordings remain readable.
    std::optional<Runtime::Duration> resultGameTime;
    std::optional<Runtime::Duration> resultRealTime;
    double recordedTimeMultiplier = 1.0;
    std::queue<Operation> operations;
    Runtime::Duration lastConsumedTimestamp{};
    Runtime::Duration lastGameTimestamp{};
    bool separateGameTime = false;
    bool gameClockPaused = false;
    std::optional<Runtime::TimePoint> recordingStart;
    std::shared_ptr<TurnPlaybackState> turnPlayback;
    int turnPlayerIndex = 0;
    bool localVersusReplay = false;
    PlaybackState playback;
    std::string checkpointIdentity;
    std::vector<PlaybackCheckpoint> checkpoints;
    Runtime::Duration nextCheckpointAt=Runtime::Duration::from_seconds(10.0);
    std::optional<std::size_t> pendingCheckpointRestore;
};

inline ReplaySessionState& session(Runtime::GameContext& context) {
    if (!context.replay_component()) throw std::logic_error("GameContext has no ReplaySessionState");
    return *context.replay_component();
}

inline const ReplaySessionState& session(const Runtime::GameContext& context) {
    if (!context.replay_component()) throw std::logic_error("GameContext has no ReplaySessionState");
    return *context.replay_component();
}

} // namespace Replay

#endif
