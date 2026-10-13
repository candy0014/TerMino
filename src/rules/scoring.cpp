#include "tetr/rules/scoring.h"

namespace Rules {

int calculate_placement_score(const ScoreInput& in) {
    int score = 0;
    if (in.linesCleared == 1) score += 100;
    if (in.linesCleared == 2) score += 300;
    if (in.linesCleared == 3) score += 500;
    if (in.linesCleared == 4) score += 800;

    const bool standardSpinPiece = in.pieceFamilyIndex == 5 &&
        Runtime::standard_rotation_system(in.pieceRotationSystem);
    if (in.rewardAllSpinPieces && in.linesCleared >= 5) score = 1200 + 400 * (in.linesCleared - 5);
    if ((standardSpinPiece || in.rewardAllSpinPieces) && in.spin) {
        if (!in.mini) {
            score = (in.linesCleared + 1) * 400;
            if (in.rewardAllSpinPieces && in.linesCleared >= 4)
                score = 2600 + 600 * (in.linesCleared - 4);
        }
        else score = (100 << in.linesCleared);
    }

    if (in.perfectClear) score += 3500;
    if (in.b2bTransition == B2BTransition::Increment && in.b2b >= 2) {
        score = static_cast<int>(score * 1.5);
    }
    if (in.combo) score += 50 * (in.combo - 1);
    return score;
}

} // namespace Rules
