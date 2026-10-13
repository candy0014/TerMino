#ifndef TETR_ENGINE_LOCKED_LINE_CLEAR_PHASE_H
#define TETR_ENGINE_LOCKED_LINE_CLEAR_PHASE_H

#include "tetr/rules/game_result.h"
#include "tetr/rules/placement_resolution.h"

#include <functional>

namespace Runtime { class Playfield; class GameContext; struct PlayerSessionState; }
namespace Piece { class Definition; }

namespace Engine {
class MovementInputState;

Rules::PlayResult run_locked_line_clear_phase(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    const Rules::PlacementResult& placement,
    const std::function<Rules::PlayResult()>& pumpFrame, MovementInputState* movementInput = nullptr);

} // namespace Engine

#endif
