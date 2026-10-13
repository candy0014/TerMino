#ifndef TETR_ENGINE_PLACEMENT_COORDINATOR_H
#define TETR_ENGINE_PLACEMENT_COORDINATOR_H
#include "tetr/runtime/piece.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/rules/game_result.h"
#include <functional>
namespace Runtime { class Playfield; }
namespace Runtime { class GameContext; struct PlayerSessionState; }
namespace Engine {
class MovementInputState;
Rules::PlayResult finalize_locked_piece(Runtime::GameContext& context,Runtime::Playfield& field,Piece::Definition& piece,
    Runtime::PlayerSessionState& player,MovementState& movementState,int blitzLevel,
    const std::function<Rules::PlayResult()>& pumpFrame, MovementInputState* movementInput = nullptr);
}
#endif
