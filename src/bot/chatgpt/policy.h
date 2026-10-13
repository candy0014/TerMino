#ifndef TETR_BOT_CHATGPT_POLICY_H
#define TETR_BOT_CHATGPT_POLICY_H

#include <algorithm>

namespace Bot_chatgpt {

struct PressureSnapshot {
    int roof = 0;
    int garbageHeight = 0;
    int incoming = 0;
    int readyIncoming = 0;
};

struct PolicyWeights {
    double attack = 560;
    double b2b = 480;
    double garbageClear = 240;
    double digPressure = 0;
    // Terminal opportunity value, not fabricated attack or a cumulative bonus.
    double comboContinuation = 0; // Retained candidate B; not used by candidate C.
};

inline double combo_reserve(const PolicyWeights& policy, int combo, int occupiedCells) {
    return occupiedCells > 0 ? policy.comboContinuation * std::clamp(combo, 0, 8) : 0.0;
}

// EXPERIMENTAL, not enabled by the host entry point. Retain the strongest
// tested digging blend for further work: it reduced digging pieces but did
// not pass the survival regression gate. Selection has no mode/test inputs.
// The fire endpoint is v0.3; the economical endpoint uses the earlier
// attack/B2B weights plus a stronger garbage-clear reward. Both share Rules
// and legality checks. Clearing urgency is distinct from physical height.
inline PolicyWeights experimental_policy_for(const PressureSnapshot& visible) {
    const double load = std::max(0, visible.roof - 6) +
        std::clamp(visible.readyIncoming, 0, 8) +
        0.5 * std::clamp(visible.incoming - visible.readyIncoming, 0, 8) +
        1.5 * std::clamp(visible.garbageHeight, 0, 8);
    const double pressure = std::clamp((load - 2.0) / 8.0, 0.0, 1.0);
    return {560.0 - 140.0 * pressure, 480.0 - 120.0 * pressure,
            240.0 + 240.0 * pressure, pressure};
}

} // namespace Bot_chatgpt
#endif
