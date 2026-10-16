#include "tetr/rules/game_completion.h"

namespace Rules {
bool time_goal_reached(const Runtime::RuntimeConfig& config,Runtime::Duration elapsed) {
    return Runtime::active_goal(config)==Runtime::GameGoal::Time && elapsed>=config.mode.blitzTime;
}
bool goal_reached(const Runtime::RuntimeConfig& config,const Runtime::ScoringState& scoring,
    int remainingInitialCells,Runtime::Duration elapsed) {
    using Runtime::GameGoal;
    const auto& mode=config.mode;
    switch(Runtime::active_goal(config)) {
        case GameGoal::None: return false;
        case GameGoal::Time: return time_goal_reached(config,elapsed);
        case GameGoal::Lines: return scoring.totalLines>=mode.racingDistance;
        case GameGoal::Garbage: return scoring.totalGarbageLines>=mode.garbageTarget;
        case GameGoal::Pieces: return scoring.placedBlocks>=mode.pieceTarget;
        case GameGoal::Attack: return scoring.totalAttack>=mode.attackTarget;
        case GameGoal::AllClear: return scoring.allClearCount>=static_cast<std::uint64_t>(mode.allClearTarget) ||
            (mode.mode==Runtime::GameMode::Racing && mode.mapGoal==Runtime::MapGoal::AllClear && scoring.mapAllClearReached);
        case GameGoal::InitialCells: return config.initialMap && remainingInitialCells==0;
    }
    return false;
}
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
