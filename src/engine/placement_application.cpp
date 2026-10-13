#include "tetr/engine/placement_application.h"

#include <algorithm>
#include "tetr/rules/quick_play.h"

namespace Engine {

PlacementApplicationBeginResult begin_placement_application(
    const PlacementApplicationState& current,
    const PlacementApplicationConfig& config,
    const Rules::PlacementResult& placement,
    Piece::Type pieceType) {
    PlacementApplicationBeginResult out;
    out.state = current;
    out.clearedGarbageLines = placement.clear.clearedGarbageLines;
    out.perfectClear = placement.clear.perfectClear;

    out.state.b2b = placement.chain.b2b;
    out.state.combo = placement.chain.combo;

    const int linesCleared = placement.clear.clearedLines;
    const int clearedGarbageLines = placement.clear.clearedGarbageLines;
    int attack = placement.attack.attack;

    if (config.garbageMode == Runtime::GarbageMode::Unclear) {
        out.effects.directGarbageLines =
            static_cast<int>(attack * config.garbageMultiple);
    }

    if (config.garbageMode == Runtime::GarbageMode::QuickPlay) {
        const auto doubleHole = Rules::apply_double_hole_reverse({
            Runtime::reverse(config.qpDoubleHole),
            attack,
            linesCleared,
            clearedGarbageLines,
            out.state.qpBlighted
        });
        attack = doubleHole.attack;
        out.state.qpBlighted = doubleHole.blighted;
    }

    const auto snakeEyes = Rules::apply_snake_eyes(
        config.qpSnakeEyes,
        linesCleared,
        attack);
    attack = snakeEyes.attack;
    out.effects.preOffsetBufferLines = snakeEyes.selfGarbage;

    if (linesCleared > 0) {
        ++out.state.clearMoves;
        out.state.zeroAttackClears += attack == 0;
        out.state.attacksAtLeast4 += attack >= 4;
        out.state.attacksAtLeast8 += attack >= 8;
        out.state.maxAttack = std::max(out.state.maxAttack, attack);
    }
    if (placement.chain.b2bTransition == Rules::B2BTransition::Increment) {
        ++out.state.difficultClears;
        out.state.b2bExtensions += current.b2b > 0;
    } else if (placement.chain.b2bTransition == Rules::B2BTransition::Break &&
               linesCleared > 0 && current.b2b > 0) {
        ++out.state.b2bBreaks;
    }
    out.state.maxB2B = std::max(out.state.maxB2B, out.state.b2b);
    out.state.maxCombo = std::max(out.state.maxCombo, out.state.combo);

    out.state.totalAttack += attack;
    ++out.state.placedBlocks;

    const int scoreDelta = placement.score;
    if (config.mode == Runtime::GameMode::Blitz && !config.blitzFlag) {
        // Keep the legacy int multiplication before accumulation into score.
        out.state.score += scoreDelta * config.blitzLevel;
    } else {
        out.state.score += scoreDelta;
    }
    out.state.totalLines += linesCleared;
    out.state.totalGarbageLines += clearedGarbageLines;

    out.effects.attack = attack;
    out.effects.needsOffset =
        Runtime::uses_garbage_buffer(config.garbageMode);
    out.effects.offsetAttack = Rules::qp_offset_attack(
        attack,
        config.qpBackfire,
        config.qpVolatile);
    out.effects.offsetClearTrigger = linesCleared +
        ((config.qpAllSpin == Runtime::AllSpinMode::Reverse && placement.spin.spin && placement.chain.previousB2B >= 5) ? 1 : 0);
    if (config.garbageMode == Runtime::GarbageMode::Multiplayer) {
        out.effects.b2bReleaseCharge = placement.chain.b2bCharging;
    }

    out.qpOperation = Rules::QPClearOperation{
        pieceType,
        linesCleared,
        placement.spin.spin,
        placement.attack.effectiveMini
    };

    return out;
}

PlacementApplicationResult finish_placement_application(
    const PlacementApplicationBeginResult& begin,
    const PlacementApplicationConfig& config,
    int rawOffsetResult) {
    PlacementApplicationResult out;
    out.state = begin.state;
    out.effects.spikeAttack = begin.effects.attack;

    const int outgoing = Rules::qp_outgoing_after_offset(
        rawOffsetResult,
        begin.effects.attack,
        config.qpBackfire,
        config.qpVolatile);
    out.effects.outgoingAttack = outgoing;
    out.state.totalOutgoingAttack += outgoing;

    if ((config.qpAllSpin == Runtime::AllSpinMode::Enabled ||
         config.qpAllSpin == Runtime::AllSpinMode::Reverse) &&
        Rules::qp_has_clear_operation(begin.qpOperation)) {
        const auto repeat = Rules::evaluate_all_spin_repeat(
            config.qpAllSpin,
            out.state.qpLastOperation,
            begin.qpOperation);
        out.effects.solidGarbageLines = repeat.solidGarbage;
        out.effects.decayTimedSolidGarbage = repeat.decayTimedSolidGarbage;
        out.state.qpLastOperation = repeat.operation;
    }

    out.effects.postOffsetBufferLines = Rules::qp_backfire_self_garbage(
        config.qpBackfire,
        outgoing);

    double experienceMultiplier = 1.0;
    if (config.qpAllSpin == Runtime::AllSpinMode::Extended && Rules::qp_has_clear_operation(begin.qpOperation)) {
        experienceMultiplier = Rules::update_extended_all_spin_history(
            out.state.qpOperations,
            Rules::extended_all_spin_signature(begin.qpOperation));
    }
    if (config.garbageMode == Runtime::GarbageMode::QuickPlay) {
        auto mods = config.qpMods;
        mods.expert = config.qpExpert;
        mods.volatileMode = config.qpVolatile;
        mods.allSpin = config.qpAllSpin;
        mods.backfire = config.qpBackfire;
        const int cancelled = begin.effects.offsetClearTrigger > 0
            ? std::max(0, begin.effects.offsetAttack - rawOffsetResult) : 0;
        Rules::qp_record_placement(out.state.qpProgress, mods, begin.qpOperation.linesCleared,
            begin.clearedGarbageLines, begin.perfectClear, begin.qpOperation.spin,
            config.pieceFamily, config.pieceColumn, cancelled, config.stackedEdgeI);
        Rules::qp_reward_placement(out.state.qpProgress, out.state.qpSpeed, out.state.qpAltitude,
            outgoing, cancelled, begin.qpOperation.linesCleared, begin.qpOperation.spin,
            out.state.totalGarbageLines, mods, experienceMultiplier);
    }

    return out;
}

} // namespace Engine
