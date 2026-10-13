#ifndef TETR_ENGINE_FRAME_COORDINATOR_H
#define TETR_ENGINE_FRAME_COORDINATOR_H

#include "tetr/rules/game_result.h"

namespace Runtime { class Playfield; }
namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Engine {

// Refresh the visibility phase after replay restore/seek as well as frames.
bool synchronize_qp_invisible_flash(Runtime::GameContext& context, Runtime::PlayerSessionState& player);

Rules::PlayResult run_frame(Runtime::GameContext& context, Runtime::Playfield& field,
                           Runtime::PlayerSessionState& player, bool pollNetwork = true,
                           bool allowImmediateGarbage = true);

} // namespace Engine

#endif
