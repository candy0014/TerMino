#ifndef TETR_RULES_SPIN_H
#define TETR_RULES_SPIN_H

#include "tetr/runtime/piece.h"
#include "tetr/runtime/spin_bonuses.h"
#include <functional>

namespace Rules {

struct SpinInput {
    Piece::SpinCheck spinChecker = Piece::SpinCheck::Immobility;
    Piece::Pose pose{};
    Runtime::RotationRecord lastRotation{};
    int mapWidth = 0;
    int mapHeight = 0;
    bool immobile = false;
    bool allSpinEnabled = false;
    bool noHoldReverse = false;
    Runtime::SpinBonuses bonuses = Runtime::SpinBonuses::AllMiniPlus;
    Runtime::RotationSystem rotationSystem = Runtime::RotationSystem::SrsPlus;
    bool grounded = true;
    bool canonicalGeometry = false;
};

struct SpinResult {
    bool spin = false;
    bool mini = false;
};

using OccupiedCell = std::function<bool(int row, int col)>;

SpinResult detect_spin(const SpinInput& input, const OccupiedCell& occupied);

} // namespace Rules

#endif
