#ifndef TETR_ENGINE_COUNTDOWN_PRESENTATION_H
#define TETR_ENGINE_COUNTDOWN_PRESENTATION_H
#include <functional>
#include "tetr/runtime/time.h"


namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Engine {
class MovementInputState;

enum class CountdownResult { Completed, Retry, OpenSettings };

CountdownResult run_start_countdown(Runtime::GameContext& context, Runtime::PlayerSessionState& first,
    Runtime::PlayerSessionState& second, MovementInputState* movementInput = nullptr,
    const std::function<bool()>& refreshLayout={},
    const std::function<bool(CountdownResult)>& allowControl={},
    const std::function<Runtime::Duration()>& remaining={});
CountdownResult run_local_versus_countdown(
    Runtime::GameContext& firstContext,
    Runtime::PlayerSessionState& first,
    Runtime::GameContext& secondContext,
    Runtime::PlayerSessionState& second,
    MovementInputState* firstMovementInput = nullptr,
    MovementInputState* secondMovementInput = nullptr,
    const std::function<bool()>& refreshLayout={});

} // namespace Engine

#endif
