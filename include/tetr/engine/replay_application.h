#ifndef TETR_ENGINE_REPLAY_APPLICATION_H
#define TETR_ENGINE_REPLAY_APPLICATION_H

#include "tetr/replay/operation.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/playfield.h"
#include "tetr/rules/game_result.h"

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Engine {

Rules::PlayResult wait_for_replay_start(
    Runtime::GameContext& context, Runtime::PlayerSessionState& player);

void apply_replay_gameplay_event(
    Runtime::GameContext& context,
    const Replay::Operation& operation,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player,
    Piece::Definition& piece);

} // namespace Engine

#endif
