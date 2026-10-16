#ifndef TETR_RULES_GAME_COMPLETION_H
#define TETR_RULES_GAME_COMPLETION_H

#include "tetr/runtime/time.h"
#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/map_definition.h"
#include "tetr/runtime/runtime_config.h"
#include "tetr/runtime/player_state.h"

namespace Rules {
bool time_goal_reached(const Runtime::RuntimeConfig& config,Runtime::Duration elapsed);
bool goal_reached(const Runtime::RuntimeConfig& config,const Runtime::ScoringState& scoring,
    int remainingInitialCells,Runtime::Duration elapsed);
bool racing_map_goal_reached(Runtime::MapGoal goal,int remainingInitialCells,bool allClearReached);

bool racing_goal_reached(
    Runtime::GameMode mode,
    Runtime::GarbageMode garbageMode,
    int clearedLines,
    int clearedGarbageLines,
    int racingDistance);

bool blitz_goal_reached(
    Runtime::GameMode mode,
    Runtime::Duration elapsedGameTime,
    Runtime::Duration blitzTime);

} // namespace Rules

#endif
