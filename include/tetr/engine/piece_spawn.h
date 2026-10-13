#ifndef TETR_ENGINE_PIECE_SPAWN_H
#define TETR_ENGINE_PIECE_SPAWN_H

#include "tetr/runtime/piece.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/movement_input_state.h"

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Engine {

MovementState make_spawn_movement_state(int width, Piece::Type type = Piece::Type::none());
bool apply_initial_movement(Runtime::GameContext& context, MovementInputState& input,
    MovementState& state, const MovementController::CanPlace& canPlace);
bool handle_spawn_collision(Runtime::GameContext& context,Runtime::Playfield& field, Piece::Definition& piece, Runtime::PlayerSessionState& player, MovementState& state);
void record_locked_piece_for_clutch(
    Runtime::PlayerSessionState& player,
    int clearedLines) noexcept;

} // namespace Engine

#endif
