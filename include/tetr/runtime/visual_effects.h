#ifndef TETR_RUNTIME_VISUAL_EFFECTS_H
#define TETR_RUNTIME_VISUAL_EFFECTS_H
#include <cstdint>
namespace Runtime {
// Local appearance percentages. Never influence rules, RNG or network state.
struct VisualEffectsConfig {
    int intensity=100,shake=35,particles=35,landing=5,clear=5,trail=5,bounciness=100;
};
// Presentation-only contacts, never serialized into rules, input or replays.
struct BoardMotionFeedback {
    std::uint64_t wallSequence=0,rotationSequence=0;
    int wallDirection=0,rotationDirection=0;
};
}
#endif
