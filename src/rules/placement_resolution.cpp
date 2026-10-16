#include "tetr/rules/placement_resolution.h"

namespace Rules {

PlacementResult resolve_placement(
    const PlacementInput& in,
    const CellValue& cellValue,
    const OccupiedCell& occupied) {
    PlacementResult out;

    out.clear = analyze_locked_field(
        in.mapWidth,
        in.firstRow,
        in.mapHeight,
        in.garbageStartRow,
        in.layerStartRow,
        cellValue);
    if(in.retainFullRows || (in.clearBatch>0 && out.clear.clearedLines<in.clearBatch)){out.clear={};out.clear.perfectClear=false;}
    out.clear.halfClear = out.clear.halfClear && in.attackRules.halfClearEnabled;
    out.allClear=out.clear.perfectClear;
    out.clear.perfectClear = out.clear.perfectClear && in.attackRules.perfectClearEnabled;

    SpinInput spinInput;
    spinInput.spinChecker = in.spinChecker;
    spinInput.pose = in.pose;
    spinInput.lastRotation = in.lastRotation;
    spinInput.mapWidth = in.mapWidth;
    spinInput.mapHeight = in.mapHeight;
    spinInput.immobile = in.immobile;
    spinInput.grounded = in.grounded;
    spinInput.allSpinEnabled = Runtime::enabled(in.allSpinMod);
    spinInput.noHoldReverse = Runtime::reverse(in.noHoldMod);
    spinInput.bonuses = in.spinBonuses;
    spinInput.rotationSystem = in.piece.rotationSystem;
    spinInput.canonicalGeometry = in.canonicalGeometry;
    out.spin = detect_spin(spinInput, occupied);

    out.chain = update_chain(ChainInput{
        out.clear.clearedLines,
        out.spin.spin,
        out.clear.perfectClear,
        in.allSpinMod,
        in.currentB2B,
        in.currentCombo,
        in.garbageMode,
        in.attackRules,
        out.clear.halfClear
    });

    AttackInput attackInput;
    attackInput.linesCleared = out.clear.clearedLines;
    attackInput.spin = out.spin.spin;
    attackInput.mini = out.spin.mini;
    attackInput.perfectClear = out.clear.perfectClear;
    attackInput.b2b = out.chain.b2b;
    attackInput.combo = out.chain.combo;
    attackInput.previousB2B = out.chain.previousB2B;
    attackInput.b2bCharging = out.chain.b2bCharging;
    attackInput.b2bTransition = out.chain.b2bTransition;
    attackInput.clearedGarbageLines = out.clear.clearedGarbageLines;
    attackInput.roundingRoll = in.attackRoundingRoll;
    attackInput.garbageMode = in.garbageMode;
    attackInput.piece = in.piece;
    attackInput.qp = in.qp;
    attackInput.spinBonuses = in.spinBonuses;
    attackInput.garbageMultiplier = in.garbageMultiplier;
    attackInput.attackRules = in.attackRules;
    attackInput.halfClear = out.clear.halfClear;
    out.attack = calculate_attack(attackInput);

    out.score = calculate_placement_score(ScoreInput{
        out.clear.clearedLines,
        out.spin.spin,
        out.attack.effectiveMini,
        out.clear.perfectClear,
        out.chain.b2b,
        out.chain.combo,
        out.chain.b2bTransition,
        in.piece.familyIndex,
        in.piece.rotationSystem,
        !in.legacySpinScoring
    });

    return out;
}

} // namespace Rules
