#ifndef TETR_ENGINE_ENTRY_DELAY_H
#define TETR_ENGINE_ENTRY_DELAY_H
#include "tetr/rules/game_result.h"
#include <functional>
namespace Runtime { class GameContext; struct PlayerSessionState; }
namespace Engine {
class MovementInputState;
void schedule_entry_delay(Runtime::GameContext& context, Runtime::PlayerSessionState& player);
Rules::PlayResult wait_for_entry_delay(Runtime::GameContext& context, Runtime::PlayerSessionState& player,
    const std::function<Rules::PlayResult()>& pumpFrame, MovementInputState* movementInput = nullptr);
}
#endif
