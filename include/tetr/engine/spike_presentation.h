#ifndef TETR_ENGINE_SPIKE_PRESENTATION_H
#define TETR_ENGINE_SPIKE_PRESENTATION_H

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Engine {

// Uses the active live/replay timeline, updates the earlier-style accumulator,
// and immediately repaints its fixed left-side display area.
void present_spike_attack(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    int attack);

// Called from frame presentation so an idle spike disappears after one second.
void refresh_spike_display(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player);

} // namespace Engine

#endif
