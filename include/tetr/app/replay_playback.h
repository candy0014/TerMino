#ifndef TETR_APP_REPLAY_PLAYBACK_H
#define TETR_APP_REPLAY_PLAYBACK_H

#include "tetr/runtime/piece.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/rules/game_result.h"

#include <functional>

namespace Runtime { class Playfield; class GameContext; struct PlayerSessionState; }

namespace App {

Rules::PlayResult playback_replay_piece(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    Engine::MovementState& movementState,
    Engine::MovementInputState& inputState,
    const std::function<Rules::PlayResult()>& pumpFrame);

} // namespace App

#endif
