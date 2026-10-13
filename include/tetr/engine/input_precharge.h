#ifndef TETR_ENGINE_INPUT_PRECHARGE_H
#define TETR_ENGINE_INPUT_PRECHARGE_H

#include "tetr/runtime/time.h"

namespace Runtime { class GameContext; }
namespace Engine {
class MovementInputState;

// Observe horizontal presses/releases without moving a piece or consuming
// gameplay commands. Charging uses wall time through countdown/clear/ARE.
void precharge_movement_input(Runtime::GameContext& context, MovementInputState& input);
// A separate input state expressed in wall-clock coordinates lets DAS charge
// while the game's movement/lock/garbage clock is frozen.
void precharge_paused_movement_input(Runtime::GameContext& context,
    MovementInputState& input, Runtime::TimePoint wallNow);
void finish_start_precharge(Runtime::GameContext& context, MovementInputState& input);
}
#endif
