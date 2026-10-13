#ifndef TETR_ENGINE_LIVE_PIECE_CONTROLS_H
#define TETR_ENGINE_LIVE_PIECE_CONTROLS_H

#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/rules/game_result.h"
#include "tetr/runtime/piece.h"

#include <functional>
#include <vector>

namespace Runtime { class Playfield; class GameContext; struct PlayerSessionState; }

namespace Engine {

struct LivePieceControlResult {
    Rules::PlayResult signal = Rules::PlayResult::ContinueFrame;
    bool hardDrop = false;
    bool locked = false;
    bool movementRecorded = false;
};

LivePieceControlResult process_live_piece_controls(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    MovementState& movementState,
    MovementInputState& inputState,
    AutoplayCarryState& autoplayCarry,
    bool heldPieceContinuation,
    Piece::Pose poseBeforeInput,
    const std::vector<int>& rotateKeys,
    const std::vector<int>& moveKeys,
    MovementController& movement,
    const MovementController::CanPlace& canPlace,
    const MovementController::TryRotate& tryRotate,
    const std::function<Rules::PlayResult()>& pumpFrame);

} // namespace Engine
#endif
