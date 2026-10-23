#include "tetr/compat/legacy_encoding.h"
#include "tetr/engine/placement_application.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

namespace TetrTests::placement_application_test {
namespace {

using Engine::PlacementApplicationBeginResult;
using Engine::PlacementApplicationConfig;
using Engine::PlacementApplicationResult;
using Engine::PlacementApplicationState;

bool close(double a, double b) {
    return std::abs(a - b) <= 1e-12 * (1.0 + std::abs(a) + std::abs(b));
}

PlacementApplicationBeginResult legacy_begin(
    const PlacementApplicationState& current,
    const PlacementApplicationConfig& config,
    const Rules::PlacementResult& placement,
    int pieceType) {
    PlacementApplicationBeginResult out;
    const int mode = Compat::to_legacy(config.mode);
    const int garbageMode = Compat::to_legacy(config.garbageMode);
    const int doubleHole = Compat::to_legacy(config.qpDoubleHole);
    const int snakeEyes = Compat::to_legacy(config.qpSnakeEyes);
    const int backfire = Compat::to_legacy(config.qpBackfire);
    const int volatileMode = Compat::to_legacy(config.qpVolatile);
    const int allSpin = Compat::to_legacy(config.qpAllSpin);
    out.state = current;
    out.state.b2b = placement.chain.b2b;
    out.state.combo = placement.chain.combo;

    int attack = placement.attack.attack;
    const int lines = placement.clear.clearedLines;
    const int garbageLines = placement.clear.clearedGarbageLines;

    if (garbageMode == 2) {
        out.effects.directGarbageLines = static_cast<int>(attack * config.garbageMultiple);
    }

    if (garbageMode == 10) {
        const auto r = Rules::apply_double_hole_reverse({
            doubleHole == 2,
            attack,
            lines,
            garbageLines,
            out.state.qpBlighted
        });
        attack = r.attack;
        out.state.qpBlighted = r.blighted;
    }

    const auto snake = Rules::apply_snake_eyes(
        Compat::modifier_mode_from_legacy(snakeEyes), lines, attack);
    if (snake.selfGarbage) out.effects.preOffsetBufferLines = snake.selfGarbage;
    attack = snake.attack;

    out.state.totalAttack += attack;
    ++out.state.placedBlocks;
    if (mode == 2 && !config.blitzFlag) {
        out.state.score += placement.score * config.blitzLevel;
    } else {
        out.state.score += placement.score;
    }
    out.state.totalLines += lines;
    out.state.totalGarbageLines += garbageLines;

    out.effects.attack = attack;
    out.effects.needsOffset =
        garbageMode == 1 || garbageMode == 5 ||
        garbageMode == 11 || garbageMode == 10;
    out.effects.offsetAttack =
        ((backfire != 2) ? attack : 0) << (volatileMode == 1);
    out.effects.offsetClearTrigger =
        lines + (allSpin == 2 && placement.spin.spin && placement.chain.previousB2B >= 5);
    if (garbageMode == 11) {
        out.effects.b2bReleaseCharge = placement.chain.b2bCharging;
    }

    out.qpOperation = {
        Compat::piece_type_from_legacy(pieceType),
        lines,
        placement.spin.spin,
        placement.attack.effectiveMini
    };
    return out;
}

PlacementApplicationResult legacy_finish(
    const PlacementApplicationBeginResult& begin,
    const PlacementApplicationConfig& config,
    int rawOffsetResult) {
    PlacementApplicationResult out;
    const int expert = Compat::to_legacy(config.qpExpert);
    const int volatileMode = Compat::to_legacy(config.qpVolatile);
    const int allSpin = Compat::to_legacy(config.qpAllSpin);
    const int backfire = Compat::to_legacy(config.qpBackfire);
    out.state = begin.state;
    out.effects.spikeAttack = begin.effects.attack;

    int send = rawOffsetResult;
    if (backfire == 2) send = begin.effects.attack;
    else if (volatileMode == 1) send >>= 1;
    out.effects.outgoingAttack = send;

    if ((allSpin == 1 || allSpin == 2) &&
        Rules::qp_has_clear_operation(begin.qpOperation)) {
        const auto repeat = Rules::evaluate_all_spin_repeat(
            Compat::all_spin_mode_from_legacy(allSpin),
            out.state.qpLastOperation,
            begin.qpOperation);
        out.effects.solidGarbageLines = repeat.solidGarbage;
        out.effects.decayTimedSolidGarbage = repeat.decayTimedSolidGarbage;
        out.state.qpLastOperation = repeat.operation;
    }

    if (backfire != 0 && send >= 2) {
        out.effects.postOffsetBufferLines = send >> 1;
    }

    if (allSpin == 3 && Rules::qp_has_clear_operation(begin.qpOperation)) {
        Rules::update_extended_all_spin_history(
            out.state.qpOperations,
            Rules::extended_all_spin_signature(begin.qpOperation));
    }

    return out;
}

void assert_state_equal(
    const PlacementApplicationState& a,
    const PlacementApplicationState& b) {
    assert(a.placedBlocks == b.placedBlocks);
    assert(a.totalAttack == b.totalAttack);
    assert(a.totalLines == b.totalLines);
    assert(a.totalGarbageLines == b.totalGarbageLines);
    assert(a.score == b.score);
    assert(a.b2b == b.b2b);
    assert(a.combo == b.combo);
    assert(a.qpBlighted == b.qpBlighted);
    assert(a.qpLastOperation == b.qpLastOperation);
    assert(a.qpOperations == b.qpOperations);
    assert(close(a.qpAltitude, b.qpAltitude));
    assert(close(a.qpSpeed, b.qpSpeed));
}

void assert_begin_equal(
    const PlacementApplicationBeginResult& a,
    const PlacementApplicationBeginResult& b) {
    assert_state_equal(a.state, b.state);
    assert(a.effects.directGarbageLines == b.effects.directGarbageLines);
    assert(a.effects.preOffsetBufferLines == b.effects.preOffsetBufferLines);
    assert(a.effects.attack == b.effects.attack);
    assert(a.effects.needsOffset == b.effects.needsOffset);
    assert(a.effects.offsetAttack == b.effects.offsetAttack);
    assert(a.effects.offsetClearTrigger == b.effects.offsetClearTrigger);
    assert(a.effects.b2bReleaseCharge == b.effects.b2bReleaseCharge);
    assert(a.qpOperation.pieceType == b.qpOperation.pieceType);
    assert(a.qpOperation.linesCleared == b.qpOperation.linesCleared);
    assert(a.qpOperation.spin == b.qpOperation.spin);
    assert(a.qpOperation.mini == b.qpOperation.mini);
}

void assert_result_equal(
    const PlacementApplicationResult& a,
    const PlacementApplicationResult& b) {
    assert_state_equal(a.state, b.state);
    assert(a.effects.spikeAttack == b.effects.spikeAttack);
    assert(a.effects.outgoingAttack == b.effects.outgoingAttack);
    assert(a.effects.postOffsetBufferLines == b.effects.postOffsetBufferLines);
    assert(a.effects.solidGarbageLines == b.effects.solidGarbageLines);
    assert(a.effects.decayTimedSolidGarbage == b.effects.decayTimedSolidGarbage);
}

Runtime::GarbageMode pick_garbage_model(std::mt19937& rng) {
    static const int models[] = {0, 1, 2, 3, 5, 11};
    return Compat::garbage_mode_from_legacy(
        models[rng() % (sizeof(models) / sizeof(models[0]))]);
}

Rules::QPClearOperation random_operation(std::mt19937& rng) {
    return {
        Compat::piece_type_from_legacy(static_cast<int>(rng() % 173)),
        static_cast<int>(rng() % 7),
        (rng() & 1U) != 0,
        (rng() & 1U) != 0
    };
}

} // namespace

int run(int argc,char** argv) {
    std::mt19937 rng(0x51A6E5u);

    for (int iteration = 0; iteration < 100000; ++iteration) {
        PlacementApplicationState state;
        state.placedBlocks = static_cast<int>(rng() % 500);
        state.totalAttack = static_cast<int>(rng() % 10000);
        state.totalLines = static_cast<int>(rng() % 10000);
        state.totalGarbageLines = static_cast<int>(rng() % 10000);
        state.score = static_cast<long long>(rng()) * (rng() % 1000);
        state.b2b = static_cast<int>(rng() % 50);
        state.combo = static_cast<int>(rng() % 30);
        state.qpBlighted = (rng() & 1U) != 0;
        if ((rng() % 4) == 0) state.qpLastOperation.reset();
        else state.qpLastOperation = random_operation(rng);
        const int historySize = static_cast<int>(rng() % 37);
        for (int i = 0; i < historySize; ++i) {
            state.qpOperations.push_back(
                Rules::extended_all_spin_signature(random_operation(rng)));
        }
        state.qpAltitude = (rng() % 100000) / 17.0;
        state.qpSpeed = (rng() % 10000) / 123.0;

        PlacementApplicationConfig config;
        config.mode = Compat::game_mode_from_legacy(static_cast<int>(rng() % 5));
        config.blitzFlag = (rng() & 1U) != 0;
        config.blitzLevel = 1 + static_cast<int>(rng() % 20);
        config.garbageMode = pick_garbage_model(rng);
        config.garbageMultiple = (rng() % 500) / 100.0;
        config.qpExpert = Compat::modifier_mode_from_legacy(static_cast<int>(rng() % 3));
        config.qpVolatile = Compat::modifier_mode_from_legacy(static_cast<int>(rng() % 3));
        config.qpDoubleHole = Compat::modifier_mode_from_legacy(static_cast<int>(rng() % 3));
        config.qpAllSpin = Compat::all_spin_mode_from_legacy(static_cast<int>(rng() % 4));
        config.qpBackfire = Compat::modifier_mode_from_legacy(static_cast<int>(rng() % 3));
        config.qpSnakeEyes = Compat::modifier_mode_from_legacy(static_cast<int>(rng() % 3));

        Rules::PlacementResult placement;
        placement.clear.clearedLines = static_cast<int>(rng() % 7);
        placement.clear.clearedGarbageLines = static_cast<int>(rng() % 7);
        placement.clear.perfectClear = static_cast<int>(rng() & 1U);
        placement.spin.spin = (rng() & 1U) != 0;
        placement.spin.mini = (rng() & 1U) != 0;
        placement.chain.b2b = static_cast<int>(rng() % 50);
        placement.chain.combo = static_cast<int>(rng() % 30);
        placement.chain.b2bCharging = static_cast<int>(rng() % 50);
        placement.attack.attack = static_cast<int>(rng() % 50);
        placement.attack.effectiveMini = (rng() & 1U) != 0;
        placement.score = static_cast<int>(rng() % 100000);

        const int pieceType = static_cast<int>(rng() % 173);
        const int rawOffset = static_cast<int>(rng() % 100);

        const auto expectedBegin = legacy_begin(state, config, placement, pieceType);
        const auto actualBegin = Engine::begin_placement_application(
            state, config, placement, Piece::Type{pieceType});
        assert_begin_equal(expectedBegin, actualBegin);

        const auto expected = legacy_finish(expectedBegin, config, rawOffset);
        const auto actual = Engine::finish_placement_application(
            actualBegin, config, rawOffset);
        assert_result_equal(expected, actual);
    }

    // Snake Eyes reverse garbage is deliberately pre-offset.
    {
        PlacementApplicationState state;
        PlacementApplicationConfig config;
        config.garbageMode = Runtime::GarbageMode::QuickPlay;
        config.qpSnakeEyes = Runtime::ModifierMode::Reverse;
        Rules::PlacementResult placement;
        placement.clear.clearedLines = 2;
        placement.attack.attack = 7;
        const auto begin = Engine::begin_placement_application(state, config, placement, Piece::Type{5});
        assert(begin.effects.preOffsetBufferLines == 7);
        assert(begin.effects.attack == 0);
    }

    // Backfire garbage is deliberately post-offset and depends on outgoing.
    {
        PlacementApplicationState state;
        PlacementApplicationConfig config;
        config.garbageMode = Runtime::GarbageMode::QuickPlay;
        config.qpBackfire = Runtime::ModifierMode::Enabled;
        Rules::PlacementResult placement;
        placement.clear.clearedLines = 1;
        placement.attack.attack = 8;
        const auto begin = Engine::begin_placement_application(state, config, placement, Piece::Type{0});
        assert(begin.effects.preOffsetBufferLines == 0);
        const auto result = Engine::finish_placement_application(begin, config, 6);
        assert(result.effects.outgoingAttack == 6);
        assert(result.effects.postOffsetBufferLines == 3);
    }

    // Combat diagnostics use the same resolved placement and post-offset attack
    // as gameplay, so Local Versus can compare BOT strategy dimensions directly.
    {
        PlacementApplicationState state;
        state.b2b = 3;
        PlacementApplicationConfig config;
        config.garbageMode = Runtime::GarbageMode::Multiplayer;
        Rules::PlacementResult placement;
        placement.clear.clearedLines = 2;
        placement.clear.clearedGarbageLines = 1;
        placement.chain.previousB2B = 3;
        placement.chain.b2bTransition = Rules::B2BTransition::Increment;
        placement.chain.b2b = 4;
        placement.chain.combo = 2;
        placement.attack.attack = 8;

        const auto begin = Engine::begin_placement_application(
            state, config, placement, Piece::Type{5});
        assert(begin.state.difficultClears == 1);
        assert(begin.state.b2bExtensions == 1);
        assert(begin.state.b2bBreaks == 0);
        assert(begin.state.maxB2B == 4);
        assert(begin.state.maxCombo == 2);
        assert(begin.state.clearMoves == 1);
        assert(begin.state.zeroAttackClears == 0);
        assert(begin.state.attacksAtLeast4 == 1);
        assert(begin.state.attacksAtLeast8 == 1);
        assert(begin.state.maxAttack == 8);

        const auto result = Engine::finish_placement_application(begin, config, 5);
        // All 8 generated lines belong to the spike even though garbage
        // cancellation leaves only 5 lines to send.
        assert(result.effects.spikeAttack == 8);
        assert(result.effects.outgoingAttack == 5);
        assert(result.state.totalOutgoingAttack == 5);

        auto breakPlacement = placement;
        breakPlacement.clear.clearedLines = 1;
        breakPlacement.chain.b2bTransition = Rules::B2BTransition::Break;
        breakPlacement.chain.b2b = 0;
        breakPlacement.chain.b2bCharging = 11;
        breakPlacement.chain.combo = 3;
        breakPlacement.attack.attack = 0;
        const auto afterBreak = Engine::begin_placement_application(
            result.state, config, breakPlacement, Piece::Type{1});
        assert(afterBreak.state.b2bBreaks == 1);
        assert(afterBreak.state.zeroAttackClears == 1);
        assert(afterBreak.effects.b2bReleaseCharge == 11);
    }

    std::cout << "placement application regression: OK\n";
    return 0;
}
} // namespace TetrTests::placement_application_test
