#include "tetr/rules/spike.h"

#include "tetr/runtime/player_session_state.h"

#include <limits>

namespace Rules {

void add_spike_attack(
    Runtime::SpikeDisplayState& state,
    int attack,
    Runtime::TimePoint now) {
    if (attack <= 0) return;

    const bool continues = state.lastAttackAt &&
        now >= *state.lastAttackAt &&
        now - *state.lastAttackAt < kSpikeWindow;
    if (!continues) {
        state.attack = attack;
    } else if (attack > std::numeric_limits<int>::max() - state.attack) {
        state.attack = std::numeric_limits<int>::max();
    } else {
        state.attack += attack;
    }
    state.lastAttackAt = now;
}

bool expire_spike(
    Runtime::SpikeDisplayState& state,
    Runtime::TimePoint now) {
    if (!state.lastAttackAt ||
        now < *state.lastAttackAt ||
        now - *state.lastAttackAt < kSpikeWindow) {
        return false;
    }
    state = {};
    return true;
}

bool spike_visible(const Runtime::SpikeDisplayState& state) noexcept {
    return state.lastAttackAt && state.attack >= kMinimumVisibleSpike;
}

} // namespace Rules
