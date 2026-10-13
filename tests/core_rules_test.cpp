#include "tetr/compat/legacy_encoding.h"
#include "tetr/rules/attack.h"
#include "tetr/rules/chain.h"
#include "tetr/rules/lock_analysis.h"
#include "tetr/rules/scoring.h"
#include "tetr/rules/spin.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <random>
#include <set>
#include <utility>
#include <vector>

namespace {

int legacy_b2b_flag(const Rules::ChainInput& in) {
    int flag = 0;
    if (in.allSpinMod == Runtime::AllSpinMode::Reverse) {
        if (in.linesCleared == 0) flag = (in.spin && in.currentB2B >= 5) ? 1 : 2;
    } else {
        if (in.linesCleared == 0) flag = 2;
        if (in.linesCleared >= 4) flag = 1;
    }
    flag |= static_cast<int>(in.spin) | static_cast<int>(in.perfectClear);
    return flag;
}

Rules::ChainResult legacy_chain(const Rules::ChainInput& in) {
    const int flag = legacy_b2b_flag(in);
    Rules::ChainResult out;
    out.previousB2B = in.currentB2B;
    out.b2b = in.currentB2B;
    if (flag == 0) {
        if (out.b2b >= 5) out.b2bCharging = out.b2b - 1;
        out.b2b = 0;
        out.b2bTransition = Rules::B2BTransition::Break;
    } else if (flag == 1) {
        ++out.b2b;
        out.b2bTransition = Rules::B2BTransition::Increment;
    } else {
        out.b2bTransition = Rules::B2BTransition::Preserve;
    }
    out.combo = in.linesCleared ? in.currentCombo + 1 : 0;
    return out;
}

Rules::AttackResult legacy_attack(const Rules::AttackInput& in) {
    Rules::AttackResult out;
    out.effectiveMini = in.mini;
    const int b2bFlag = in.b2bTransition == Rules::B2BTransition::Increment ? 1 :
                        in.b2bTransition == Rules::B2BTransition::Break ? 0 : 2;
    const int allSpin = Compat::to_legacy(in.qp.allSpin);
    const int rotationSystem = Compat::to_legacy(in.piece.rotationSystem);
    const int garbageMode = Compat::to_legacy(in.garbageMode);
    const int expert = Compat::to_legacy(in.qp.expert);
    const int noHold = Compat::to_legacy(in.qp.noHold);
    const int messy = Compat::to_legacy(in.qp.messy);
    const int gravity = Compat::to_legacy(in.qp.gravity);
    const int volatileMod = Compat::to_legacy(in.qp.volatileMod);
    const int doubleHole = Compat::to_legacy(in.qp.doubleHole);
    const int invisible = Compat::to_legacy(in.qp.invisible);
    const int backfire = Compat::to_legacy(in.qp.backfire);
    const int snakeEyes = Compat::to_legacy(in.qp.snakeEyes);
    auto basic_attack = [&](int lines) {
        if (lines <= 0) return 0;
        if (in.spin && !out.effectiveMini) {
            return lines >= 4 ? 2 * (lines + 1) : 2 * lines;
        }
        if (lines == 1) return 0;
        if (lines == 2) return 1;
        if (lines == 3) return 2;
        return lines;
    };
    auto rounded_attack = [&](double raw) {
        const double integral = std::floor(raw);
        int result = static_cast<int>(integral);
        if (garbageMode == 10 && in.roundingRoll < raw - integral) ++result;
        return result;
    };
    auto combo_single_attack = [&](bool reverse) {
        const int combo = std::max(in.combo, 1) - 1;
        int result = rounded_attack(std::log(1.25 * combo + 1.0));
        if (reverse) result /= 2;
        return result;
    };
    auto garbage_special_bonus = [&] {
        return garbageMode != 10 && in.clearedGarbageLines > 0 &&
               (in.linesCleared >= 4 || in.spin)
            ? 1
            : 0;
    };

    int atk = 0;
    if (allSpin == 2) {
        if (in.linesCleared || (in.spin && in.b2b >= 5)) {
            int levelB2B = 0;
            if (in.piece.familyIndex != 5 ||
                (rotationSystem != 0 && rotationSystem != 1 && rotationSystem != 2)) {
                out.effectiveMini = false;
            }
            const int basic = basic_attack(in.linesCleared);
            if (in.b2b >= 2) levelB2B = 1;
            if (in.b2b >= 5) levelB2B = 2;
            atk = rounded_attack((basic + levelB2B) *
                (1 + (std::max(in.combo, 1) - 1) * 0.25));
            if (!basic && !levelB2B) atk = combo_single_attack(false);
            atk += static_cast<int>(in.perfectClear) * 5 + in.b2bCharging +
                   garbage_special_bonus();
        }
    } else if (in.linesCleared) {
        int levelB2B = 0;
        const int basic = basic_attack(in.linesCleared);
        if (in.b2b >= 2) levelB2B = 1;
        atk = rounded_attack((basic + levelB2B) *
            (1 + (std::max(in.combo, 1) - 1) * 0.25));
        if (!basic && !levelB2B) {
            atk = combo_single_attack(snakeEyes == 2);
        }
        if (garbageMode == 10 && in.previousB2B == 0 && in.linesCleared == 1 && b2bFlag == 0 && in.combo == 1 &&
            expert == 0 && noHold != 2 && messy != 2 && gravity != 2 && volatileMod != 2 &&
            doubleHole != 2 && invisible != 2 && allSpin != 2 && backfire != 2 && snakeEyes != 2) {
            ++atk;
        }
        atk += static_cast<int>(in.perfectClear) * 5 +
               std::max(0, in.b2bCharging - (garbageMode == 10 ? 3 : 0)) +
               garbage_special_bonus();
    }
    out.attack = atk;
    return out;
}

int legacy_score(const Rules::ScoreInput& in) {
    int score = 0;
    if (in.linesCleared == 1) score += 100;
    if (in.linesCleared == 2) score += 300;
    if (in.linesCleared == 3) score += 500;
    if (in.linesCleared == 4) score += 800;
    const int rotationSystem = Compat::to_legacy(in.pieceRotationSystem);
    if (in.pieceFamilyIndex == 5 &&
        (rotationSystem == 0 || rotationSystem == 1 || rotationSystem == 2) && in.spin) {
        if (!in.mini) score = (in.linesCleared + 1) * 400;
        else score = (100 << in.linesCleared);
    }
    if (in.perfectClear) score += 3500;
    if (in.b2bTransition == Rules::B2BTransition::Increment && in.b2b >= 2) score = static_cast<int>(score * 1.5);
    if (in.combo) score += 50 * (in.combo - 1);
    return score;
}

void test_lock_analysis() {
    constexpr int width = 4;
    constexpr int firstRow = -1;
    constexpr int endRow = 5;
    std::array<std::array<int, width>, 6> field{};
    for (auto& row : field) row.fill(-1);
    auto cell = [&](int row, int col) { return Compat::board_cell_from_legacy(field[row - firstRow][col]); };

    auto empty = Rules::analyze_locked_field(width, firstRow, endRow, 3, 4, cell);
    assert(empty.clearedLines == 0);
    assert(empty.perfectClear);

    field[4].fill(7); // row 3: garbage line
    auto one = Rules::analyze_locked_field(width, firstRow, endRow, 3, 4, cell);
    assert(one.clearedLines == 1);
    assert(one.clearedGarbageLines == 1);
    assert(one.clearedLayerLines == 0);
    assert(one.perfectClear);

    field[5].fill(7); // row 4: layer + garbage
    auto two = Rules::analyze_locked_field(width, firstRow, endRow, 3, 4, cell);
    assert(two.clearedLines == 2);
    assert(two.clearedGarbageLines == 2);
    assert(two.clearedLayerLines == 1);

    field[2][0] = 1; // partial row 1 remains after the clears
    auto partial = Rules::analyze_locked_field(width, firstRow, endRow, 3, 4, cell);
    assert(!partial.perfectClear);
}

void test_spin_detection() {
    std::set<std::pair<int, int>> occupiedCells;
    auto occupied = [&](int row, int col) { return occupiedCells.count({row, col}) != 0; };

    Rules::SpinInput in;
    in.lastRotation = Compat::rotation_record_from_legacy(1);
    in.mapWidth = 10;
    in.mapHeight = 20;
    in.pose.position = {5,5};

    in.spinChecker = Piece::SpinCheck::Immobility;
    in.immobile = true;
    auto immobileMini = Rules::detect_spin(in, occupied);
    assert(immobileMini.spin && immobileMini.mini);
    in.allSpinEnabled = true;
    auto allSpin = Rules::detect_spin(in, occupied);
    assert(allSpin.spin && !allSpin.mini);

    in = {};
    in.spinChecker = Piece::SpinCheck::TCorner;
    in.lastRotation = Compat::rotation_record_from_legacy(1);
    in.mapWidth = 10;
    in.mapHeight = 20;
    in.pose.position = {5,5};
    in.pose.rotation = Compat::rotation_from_legacy(0);
    occupiedCells = {{4,4}, {4,6}, {6,6}};
    auto full = Rules::detect_spin(in, occupied);
    assert(full.spin && !full.mini);

    occupiedCells = {{4,4}, {6,6}, {6,4}};
    auto mini = Rules::detect_spin(in, occupied);
    assert(mini.spin && mini.mini);

    in.lastRotation = Compat::rotation_record_from_legacy(5);
    auto kickUpgrade = Rules::detect_spin(in, occupied);
    assert(kickUpgrade.spin && !kickUpgrade.mini);

    in.noHoldReverse = true;
    auto reverseNoHold = Rules::detect_spin(in, occupied);
    assert(reverseNoHold.spin && reverseNoHold.mini);
}

void test_chain_equivalence() {
    for (int allSpin : {0, 1, 2, 3}) {
        for (int lines = 0; lines <= 6; ++lines) {
            for (int spin = 0; spin <= 1; ++spin) {
                for (int pc = 0; pc <= 1; ++pc) {
                    for (int b2b = 0; b2b <= 8; ++b2b) {
                        for (int combo = 0; combo <= 8; ++combo) {
                            Rules::ChainInput in{lines, spin != 0, pc != 0, Compat::all_spin_mode_from_legacy(allSpin), b2b, combo};
                            in.attackRules.legacy=true; // Historical recordings.
                            const auto expected = legacy_chain(in);
                            const auto actual = Rules::update_chain(in);
                            assert(actual.b2bTransition == expected.b2bTransition);
                            assert(actual.previousB2B == expected.previousB2B);
                            assert(actual.b2bCharging == expected.b2bCharging);
                            assert(actual.b2b == expected.b2b);
                            assert(actual.combo == expected.combo);
                        }
                    }
                }
            }
        }
    }
}

void test_attack_and_score_equivalence() {
    std::mt19937 rng(20260831);
    for (int iteration = 0; iteration < 100000; ++iteration) {
        Rules::AttackInput in;
        in.linesCleared = rng() % 7;
        in.spin = rng() % 2;
        in.mini = rng() % 2;
        in.perfectClear = rng() % 2;
        in.b2b = rng() % 10;
        in.combo = rng() % 50;
        in.previousB2B = rng() % 10;
        in.b2bCharging = rng() % 10;
        in.b2bTransition = static_cast<Rules::B2BTransition>(rng() % 3);
        in.clearedGarbageLines = rng() % 5;
        in.garbageMode = Compat::garbage_mode_from_legacy(
            (rng() % 3 == 0) ? 10 : static_cast<int>(rng() % 12));
        in.piece.familyIndex = 4 + rng() % 3;
        in.piece.rotationSystem = Compat::rotation_system_from_legacy(rng() % 7);
        in.qp.expert = Compat::modifier_mode_from_legacy(rng() % 3);
        in.qp.noHold = Compat::modifier_mode_from_legacy(rng() % 3);
        in.qp.messy = Compat::modifier_mode_from_legacy(rng() % 3);
        in.qp.gravity = Compat::modifier_mode_from_legacy(rng() % 3);
        in.qp.volatileMod = Compat::modifier_mode_from_legacy(rng() % 3);
        in.qp.doubleHole = Compat::modifier_mode_from_legacy(rng() % 3);
        in.qp.invisible = Compat::modifier_mode_from_legacy(rng() % 3);
        in.qp.allSpin = Compat::all_spin_mode_from_legacy(
            (rng() % 4 == 0) ? 2 : static_cast<int>(rng() % 4));
        in.qp.backfire = Compat::modifier_mode_from_legacy(rng() % 3);
        in.qp.snakeEyes = Compat::modifier_mode_from_legacy(rng() % 3);

        if (in.garbageMode == Runtime::GarbageMode::QuickPlay) continue;
        in.attackRules.legacy=true;
        const auto expectedAttack = legacy_attack(in);
        const auto actualAttack = Rules::calculate_attack(in);
        assert(actualAttack.attack == expectedAttack.attack);
        assert(actualAttack.effectiveMini == expectedAttack.effectiveMini);

        Rules::ScoreInput score;
        score.linesCleared = in.linesCleared;
        score.spin = in.spin;
        score.mini = actualAttack.effectiveMini;
        score.perfectClear = in.perfectClear;
        score.b2b = in.b2b;
        score.combo = in.combo;
        score.b2bTransition = in.b2bTransition;
        score.pieceFamilyIndex = in.piece.familyIndex;
        score.pieceRotationSystem = in.piece.rotationSystem;
        assert(Rules::calculate_placement_score(score) == legacy_score(score));
    }
}

void test_current_tetrio_attack_regressions() {
    Rules::AttackInput in;
    in.linesCleared = 4;
    in.b2b = 2;
    in.b2bTransition = Rules::B2BTransition::Increment;
    in.garbageMode = Runtime::GarbageMode::Multiplayer;

    // replay/bq3kbtr7wm.txt ends with B2B Quads at displayed 1- and 2-combo.
    // Clearing three/four garbage rows earns one flat Garbage Special Bonus,
    // not one point for every garbage row: 6+1 and 7+1.
    in.combo = 2;
    in.clearedGarbageLines = 3;
    assert(Rules::calculate_attack(in).attack == 7);
    in.combo = 3;
    in.clearedGarbageLines = 4;
    assert(Rules::calculate_attack(in).attack == 8);

    // The bonus stays flat regardless of how many garbage rows the special
    // clear removes, and a non-special all-clear does not receive it.
    in.combo = 2;
    in.clearedGarbageLines = 1;
    assert(Rules::calculate_attack(in).attack == 7);
    in.linesCleared = 2;
    in.spin = false;
    in.perfectClear = true;
    in.b2b = 1;
    in.combo = 1;
    assert(Rules::calculate_attack(in).attack == 6);

    // Current base table: a full Spin Quad is 10, not 8.
    in = {};
    in.linesCleared = 4;
    in.spin = true;
    in.mini = false;
    in.combo = 1;
    assert(Rules::calculate_attack(in).attack == 10);

    // Combo Singles use the documented logarithm without the old 4-line cap.
    in = {};
    in.linesCleared = 1;
    in.combo = 43; // displayed 42-combo
    assert(Rules::calculate_attack(in).attack == 3);
    in.combo = 44; // displayed 43-combo
    assert(Rules::calculate_attack(in).attack == 4);
    in.combo = 120;
    assert(Rules::calculate_attack(in).attack == 5);

    // QUICK PLAY uses weighted RNG rounding.  A displayed 1-combo Double is
    // 1.25 attack, so rolls below 0.25 round up and all other rolls round down.
    in = {};
    in.linesCleared = 2;
    in.combo = 2;
    in.garbageMode = Runtime::GarbageMode::QuickPlay;
    in.roundingRoll = 0.249999;
    assert(Rules::calculate_attack(in).attack == 2);
    in.roundingRoll = 0.25;
    assert(Rules::calculate_attack(in).attack == 1);
}

void test_current_tetrio_attack_matrix_in_every_mode() {
    static constexpr Runtime::GarbageMode modes[] = {
        Runtime::GarbageMode::None,
        Runtime::GarbageMode::Backfire,
        Runtime::GarbageMode::Unclear,
        Runtime::GarbageMode::CheeseLayer,
        Runtime::GarbageMode::CheeseTimer,
        Runtime::GarbageMode::Survival,
        Runtime::GarbageMode::Multiplayer
    };

    for (const auto mode : modes) {
        for (int currentB2B = 0; currentB2B <= 8; ++currentB2B) {
            for (int currentCombo = 0; currentCombo <= 50; ++currentCombo) {
                for (int lines = 0; lines <= 6; ++lines) {
                    for (int spin = 0; spin <= 1; ++spin) {
                        for (int mini = 0; mini <= 1; ++mini) {
                            for (int perfectClear = 0; perfectClear <= 1; ++perfectClear) {
                                const auto chain = Rules::update_chain({
                                    lines,
                                    spin != 0,
                                    perfectClear != 0,
                                    Runtime::AllSpinMode::Disabled,
                                    currentB2B,
                                    currentCombo
                                });

                                Rules::AttackInput input;
                                input.linesCleared = lines;
                                input.spin = spin != 0;
                                input.mini = mini != 0;
                                input.perfectClear = perfectClear != 0;
                                input.b2b = chain.b2b;
                                input.combo = chain.combo;
                                input.previousB2B = chain.previousB2B;
                                input.b2bCharging = chain.b2bCharging;
                                input.b2bTransition = chain.b2bTransition;
                                input.clearedGarbageLines = lines > 0
                                    ? std::min(lines, 3)
                                    : 0;
                                input.roundingRoll = 1.0;
                                input.garbageMode = mode;

                                int expected = 0;
                                if (lines > 0) {
                                    int basic = 0;
                                    if (spin != 0 && mini == 0) {
                                        basic = lines >= 4
                                            ? 2 * (lines + 1)
                                            : 2 * lines;
                                    } else if (lines == 2) {
                                        basic = 1;
                                    } else if (lines == 3) {
                                        basic = 2;
                                    } else if (lines >= 4) {
                                        basic = lines;
                                    }

                                    const int b2bBonus = chain.b2b >= 2 ? 1 : 0;
                                    const int combo = chain.combo - 1;
                                    const double raw = basic + b2bBonus == 0
                                        ? std::log(1.25 * combo + 1.0)
                                        : (basic + b2bBonus) * (1.0 + 0.25 * combo);
                                    expected = static_cast<int>(std::floor(raw));

                                    const bool cleanQPOpenerSingle =
                                        mode == Runtime::GarbageMode::QuickPlay &&
                                        currentB2B == 0 && lines == 1 && spin == 0 &&
                                        perfectClear == 0 && currentCombo == 0;
                                    expected += cleanQPOpenerSingle ? 1 : 0;
                                    expected += perfectClear != 0 ? 5 : 0;
                                    expected += std::max(
                                        0,
                                        chain.b2bCharging -
                                            (mode == Runtime::GarbageMode::QuickPlay ? 3 : 0));
                                    expected +=
                                        mode != Runtime::GarbageMode::QuickPlay &&
                                        input.clearedGarbageLines > 0 &&
                                        (lines >= 4 || spin != 0)
                                            ? 1
                                            : 0;
                                }

                                assert(Rules::calculate_attack(input).attack == expected);
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace

int main() {
    test_lock_analysis();
    test_spin_detection();
    test_chain_equivalence();
    test_attack_and_score_equivalence();
    test_current_tetrio_attack_regressions();
    test_current_tetrio_attack_matrix_in_every_mode();
    return 0;
}
