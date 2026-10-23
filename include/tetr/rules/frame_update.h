#ifndef TETR_RULES_FRAME_UPDATE_H
#define TETR_RULES_FRAME_UPDATE_H

#include <random>
#include <vector>

#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/gravity.h"
#include "tetr/runtime/time.h"
#include "tetr/runtime/runtime_config.h"
#include "tetr/runtime/quick_play_progress.h"

namespace Rules {

struct FrameRuleState {
    double qpSpeed = 1.0;
    double qpAltitude = 0.0;
    int qpFloor = 1;
    double survivalApm = 0.0;
    Runtime::Duration garbageInTime{};
    Runtime::GravityInterval gravitySpeed = Runtime::GravityInterval::instant();
    Runtime::Duration epld{};
    bool qpBagBreather = false;
    Runtime::QuickPlayProgress qpProgress;
};

struct FrameRuleConfig {
    Runtime::GarbageMode garbageMode = Runtime::GarbageMode::None;
    bool replayMode = false;
    Runtime::ModifierMode qpModExpert = Runtime::ModifierMode::Disabled;
    Runtime::ModifierMode qpModGravity = Runtime::ModifierMode::Disabled;
    double qpGarbageMultiple = 1.0;
    Runtime::Duration qpGarbageInTime = Runtime::Duration::from_seconds(1.0);
    Runtime::Duration elapsedGameTime{};
    Runtime::QuickPlayConfig qpMods;
    int garbageRows = 0;
    bool inDanger = false;
    // BOT forecasts advance known Windup/fatigue without inventing opponents.
    bool generateOpponentPressure = true;
    double ownAttackApm = 0.0;
};

struct FramePrePresentationResult {
    std::vector<int> incomingGarbagePackets;
    // Already adjusted Windup segments must not receive a second multiplier,
    // stochastic roll, protection discount or altitude penalty.
    std::vector<int> resolvedQpGarbagePackets;
    int permanentGarbageLines = 0;
};

// Advances one project frame using its elapsed delta: incoming pressure,
// booster progression, altitude smoothing, protection and fatigue. Survival's
// RNG sequence remains compatible with the existing implementation.
FramePrePresentationResult advance_frame_before_presentation(
    FrameRuleState& state,
    const FrameRuleConfig& config,
    Runtime::Duration deltaTime,
    std::mt19937& rng);

// Refreshes the floor/mod-dependent incoming delay and lock delay.
void advance_frame_after_presentation(
    FrameRuleState& state,
    const FrameRuleConfig& config,
    Runtime::Duration deltaTime);

} // namespace Rules

#endif
