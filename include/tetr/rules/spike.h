#ifndef TETR_RULES_SPIKE_H
#define TETR_RULES_SPIKE_H

#include "tetr/runtime/time.h"

namespace Runtime { struct SpikeDisplayState; }

namespace Rules {

inline constexpr int kMinimumVisibleSpike = 8;
inline constexpr Runtime::Duration kSpikeWindow =
    Runtime::Duration::from_seconds(1.0);

// Positive attacks belong to the same spike while the interval between them
// is strictly less than one second.  Zero attack does not extend the window.
void add_spike_attack(
    Runtime::SpikeDisplayState& state,
    int attack,
    Runtime::TimePoint now);

// Clears a spike after a full second without another positive attack.  The
// return value reports whether visible/stored state changed.
bool expire_spike(
    Runtime::SpikeDisplayState& state,
    Runtime::TimePoint now);

bool spike_visible(const Runtime::SpikeDisplayState& state) noexcept;

} // namespace Rules

#endif
