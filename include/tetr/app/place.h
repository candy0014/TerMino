#ifndef TETR_APP_PLACE_H
#define TETR_APP_PLACE_H

#include "tetr/runtime/piece.h"
#include "tetr/runtime/playfield.h"
#include "tetr/rules/game_result.h"
#include "tetr/engine/undo.h"

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace App {
using Result = Rules::PlayResult;

using PieceLoopState=Engine::PieceLoopState;
Result run_frame(Runtime::GameContext& context, Runtime::Playfield& field, Runtime::PlayerSessionState& player);
Result run_piece(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition block,
    Runtime::PlayerSessionState& player,
    PieceLoopState& loopState,
    bool holdContinuation = false);
}

#endif
