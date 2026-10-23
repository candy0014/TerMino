#ifndef TETR_REPLAY_SESSION_STATE_H
#define TETR_REPLAY_SESSION_STATE_H

#include "tetr/replay/operation.h"
#include "tetr/runtime/game_context.h"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <functional>
#include <string_view>

namespace Replay {
class TetrioRecorder;
struct MultiplayerReplay;

struct TurnPlaybackState {
    double quotas[2]{};
    std::atomic<std::uint64_t> completedPieces[2]{};
    std::atomic<bool> ended[2]{};
};

// A local-versus recording has two decoding workers but one viewer timeline.
// Only this small control/clock state crosses the workers; boards and operation
// queues remain owned by their respective player threads.
struct SharedPlaybackState {
    std::recursive_mutex mutex;
    Runtime::Duration elapsed{};
    Runtime::TimePoint anchor{};
    double speed = 1.0;
    bool paused = false;
    std::function<std::array<bool,5>()> readControls;
    std::array<bool,5> controlsDown{};
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
        std::shared_ptr<SharedPlaybackState> shared;
        Runtime::Duration sharedClockOffset{};
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
    std::size_t recordingLimit=10000000;
    std::function<void(std::string_view)> recordObserver;
    std::shared_ptr<MultiplayerReplay> multiplayerReplay;
    bool onlineRecording=false;
    std::array<std::uint64_t,3> onlineIdentity{}; // room, match, actor; never a credential
    std::uint64_t onlineProfile=0;
    // Preserve the complete source for Save As, including both local players.
    std::string loadedRecording;
    std::string sourcePath;
    // Bind once to stable history/run IDs so repeated Save As and seeking can
    // update the same plays while sourcePath remains the playback input.
    struct ChallengeRecordLink {std::string historyPath;std::uint64_t runId=0;};
    std::vector<ChallengeRecordLink> challengeRecords;
    std::string recentPath;
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
    bool challengePrefixActive = false;
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
