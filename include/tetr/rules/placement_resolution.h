#ifndef TETR_RULES_PLACEMENT_RESOLUTION_H
#define TETR_RULES_PLACEMENT_RESOLUTION_H

#include "tetr/rules/attack.h"
#include "tetr/rules/chain.h"
#include "tetr/rules/lock_analysis.h"
#include "tetr/rules/scoring.h"
#include "tetr/rules/spin.h"

namespace Rules {

struct PlacementInput {
    int clearBatch=0;
    int mapWidth = 0;
    int mapHeight = 0;
    int firstRow = 0;
    int garbageStartRow = 0;
    int layerStartRow = 0;

    Piece::Pose pose{};
    Runtime::RotationRecord lastRotation{};
    bool immobile = false;
    Piece::SpinCheck spinChecker = Piece::SpinCheck::Immobility;

    int currentB2B = 0;
    int currentCombo = 0;
    double attackRoundingRoll = 1.0;
    Runtime::GarbageMode garbageMode = Runtime::GarbageMode::None;
    Runtime::AllSpinMode allSpinMod = Runtime::AllSpinMode::Disabled;
    Runtime::ModifierMode noHoldMod = Runtime::ModifierMode::Disabled;

    PieceAttackTraits piece;
    QPAttackModifiers qp;
    Runtime::SpinBonuses spinBonuses = Runtime::SpinBonuses::AllMiniPlus;
    bool legacySpinScoring = false;
    bool grounded = true;
    bool canonicalGeometry = false;
    double garbageMultiplier = 1.0;
    Runtime::AttackRules attackRules;
    bool retainFullRows=false;
};

struct PlacementResult {
    ClearAnalysis clear;
    SpinResult spin;
    ChainResult chain;
    AttackResult attack;
    int score = 0;
    bool allClear = false;
};

PlacementResult resolve_placement(
    const PlacementInput& input,
    const CellValue& cellValue,
    const OccupiedCell& occupied);

} // namespace Rules

#endif
