#ifndef TETR_RUNTIME_SESSION_STATE_H
#define TETR_RUNTIME_SESSION_STATE_H

#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/quick_play_progress.h"
#include "tetr/runtime/gravity.h"
#include "tetr/runtime/time.h"

#include <optional>
#include <cstdint>

namespace Runtime {

struct QuickPlaySessionState {
    double speed = 1.0; // Booster level; retained name at the replay boundary.
    double altitude = 0.0;
    int floor = 1;
    double garbageMultiple = 1.0;
    Duration garbageInTime = Duration::from_seconds(1.0);
    QuickPlayProgress progress;
};

struct DynamicRuleState {
    std::optional<GravityInterval> gravityInterval;
    std::optional<Duration> epld;
    std::optional<double> survivalApm;
    std::optional<Duration> incomingGarbageDelay;

    void clear() {
        gravityInterval.reset();
        epld.reset();
        survivalApm.reset();
        incomingGarbageDelay.reset();
    }
};

// Mutable state belonging to one active game/session. PlayerSessionState is
// the actual per-player runtime model. The historical compatibility facade no longer
// participates in normal session ownership and is isolated to the BOT bridge.
struct SessionState {
    PlayerSessionState player;
    PlayerSessionState opponent;
    std::optional<TimePoint> gameStartTime;
    QuickPlaySessionState quickPlay;
    DynamicRuleState dynamicRules;
    TimePoint simulationTime{};
    std::uint64_t completedSimulationPieces = 0;
    Duration turnWaitDuration{};
    std::optional<TimePoint> turnWaitStarted;
    // Real time removed by undo; game timers remain on their saved timeline.
    Duration clockOffset{};
};

inline Duration elapsed_game_time(const SessionState& session, TimePoint now) noexcept {
    return session.gameStartTime ? now - *session.gameStartTime : Duration{};
}

} // namespace Runtime

#endif
