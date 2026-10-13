#include "tetr/rules/game_completion.h"

namespace Rules {
bool racing_map_goal_reached(Runtime::MapGoal goal,int remainingInitialCells,bool allClearReached) {
    if(goal==Runtime::MapGoal::ClearInitial) return remainingInitialCells==0;
    if(goal==Runtime::MapGoal::AllClear) return allClearReached;
    return false;
}

bool racing_goal_reached(
    Runtime::GameMode mode,
    Runtime::GarbageMode garbageMode,
    int clearedLines,
    int clearedGarbageLines,
    int racingDistance) {
    if(mode != Runtime::GameMode::Racing || clearedLines < racingDistance) return false;
    if(Runtime::is_cheese_mode(garbageMode)) return clearedGarbageLines>=racingDistance;
    return true;
}

bool blitz_goal_reached(Runtime::GameMode mode,Runtime::Duration elapsedGameTime,Runtime::Duration blitzTime) {
    return mode == Runtime::GameMode::Blitz && elapsedGameTime > blitzTime;
}

} // namespace Rules
