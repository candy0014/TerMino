#ifndef TETR_ENGINE_SIMULATION_TIME_H
#define TETR_ENGINE_SIMULATION_TIME_H

#include "tetr/rules/game_result.h"

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Engine {

// Called after the piece and its line-clear animation have completely settled.
Rules::PlayResult advance_piece_time(Runtime::GameContext& context,
                                    Runtime::PlayerSessionState& player);

// Runs before spawning a fresh piece (Hold remains part of the same piece).
Rules::PlayResult wait_for_piece_turn(Runtime::GameContext& context,
                                     Runtime::PlayerSessionState& player);

} // namespace Engine
#endif
