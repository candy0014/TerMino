#ifndef TETR_ENGINE_LIVE_PIECE_CONTROLLER_H
#define TETR_ENGINE_LIVE_PIECE_CONTROLLER_H
#include "tetr/runtime/piece.h"
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/rules/game_result.h"
#include <functional>
namespace Runtime { class Playfield; }
namespace Runtime { class GameContext; struct PlayerSessionState; }
namespace Engine {
Rules::PlayResult run_live_piece_movement(Runtime::GameContext& context,Runtime::Playfield& field,Piece::Definition& piece,
    Runtime::PlayerSessionState& player,MovementState& movementState,MovementInputState& inputState,
    AutoplayCarryState& autoplayCarry,bool heldPieceContinuation,const MovementController::CanPlace& canPlace,
    const MovementController::TryRotate& tryRotate,const std::function<Rules::PlayResult()>& pumpFrame);
}
#endif
