#ifndef TETR_RULES_SCORING_H
#define TETR_RULES_SCORING_H

#include "tetr/rules/chain.h"
#include "tetr/runtime/game_modes.h"

namespace Rules {

struct ScoreInput {
    int linesCleared = 0;
    bool spin = false;
    bool mini = false;
    bool perfectClear = false;
    int b2b = 0;
    int combo = 0;
    B2BTransition b2bTransition = B2BTransition::Break;
    int pieceFamilyIndex = 4;
    Runtime::RotationSystem pieceRotationSystem = Runtime::RotationSystem::SrsPlus;
    bool rewardAllSpinPieces = false;
};

int calculate_placement_score(const ScoreInput& input);

} // namespace Rules

#endif
