#include "tetr/rules/attack.h"
#include "tetr/rules/quick_play.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Rules {
namespace {

bool standard_spin_piece(const PieceAttackTraits& piece) {
    return piece.familyIndex == 5 &&
           Runtime::standard_rotation_system(piece.rotationSystem);
}

int basic_attack(int linesCleared, bool fullSpin) {
    if (linesCleared <= 0) return 0;
    if (fullSpin) {
        // Current TETR.IO full-Spin row is 2/4/6/10.  This extension also
        // covers the 5+ clears possible with larger event pieces.
        return linesCleared >= 4
            ? 2 * (linesCleared + 1)
            : 2 * linesCleared;
    }
    if (linesCleared == 1) return 0;
    if (linesCleared == 2) return 1;
    if (linesCleared == 3) return 2;
    return linesCleared;
}

int spin_base_attack(int basic, const AttackInput& in) {
    // HANDHELD halves only the non-T spin base, before B2B and combo bonuses.
    return in.spinBonuses == Runtime::SpinBonuses::Handheld && in.spin &&
        !standard_spin_piece(in.piece) ? basic / 2 : basic;
}

int round_attack(double rawAttack, const AttackInput& in) {
    rawAttack = std::clamp(rawAttack * in.garbageMultiplier, 0.0, static_cast<double>(std::numeric_limits<int>::max()));
    const double integral = std::floor(rawAttack);
    int attack = static_cast<int>(integral);
    if (in.garbageMode == Runtime::GarbageMode::QuickPlay &&
        in.roundingRoll < rawAttack - integral) {
        ++attack;
    }
    return attack;
}

int combo_single_attack(const AttackInput& in, bool reverseSnakeEyes) {
    const int combo = std::max(in.combo, 1) - 1;
    int attack = round_attack(std::log(1.25 * combo + 1.0), in);
    if (reverseSnakeEyes) attack /= 2;
    return attack;
}

int garbage_special_bonus(const AttackInput& in) {
    // BETA 1.3's Garbage Special Bonus is a flat +1 for a Quad/Spin that
    // clears one or more garbage rows.  It is not one point per row and is
    // disabled in QUICK PLAY.
    return in.garbageMode != Runtime::GarbageMode::QuickPlay &&
           in.clearedGarbageLines > 0 &&
           (in.linesCleared >= 4 || in.spin)
        ? 1
        : 0;
}

double configured_b2b_bonus(const AttackInput& in) {
    if (in.b2bTransition != B2BTransition::Increment || in.b2b < 2 ||
        in.attackRules.b2bReward == Runtime::B2BReward::None) return 0;
    if (in.attackRules.b2bReward == Runtime::B2BReward::FixedAttack)
        return std::max(0, in.attackRules.b2bRewardAmount);
    if (in.b2b == 2) return 1;
    const double raw = 1 + std::log(0.8 * (in.b2b - 1) + 1);
    const double integer = std::floor(raw);
    return integer + (1 + raw - integer) / 3;
}

AttackResult configured_attack(const AttackInput& in) {
    AttackResult out;
    out.effectiveMini = in.mini;
    const bool pc = in.perfectClear && in.attackRules.perfectClearEnabled;
    const bool hc = !in.perfectClear && in.halfClear && in.attackRules.halfClearEnabled;
    const double clearBonus = pc ? in.attackRules.perfectClearAttack : hc ? in.attackRules.halfClearAttack : 0;
    out.perfectClearAttack = round_attack(pc ? clearBonus : 0, in);
    out.halfClearAttack = round_attack(hc ? clearBonus : 0, in);
    const bool qp = in.garbageMode == Runtime::GarbageMode::QuickPlay;
    const bool reverseAllSpin = in.qp.allSpin == Runtime::AllSpinMode::Reverse;
    if (qp && Runtime::reverse(in.qp.noHold)) out.effectiveMini = in.spin;
    else if (Runtime::enabled(in.qp.allSpin) && !standard_spin_piece(in.piece)) out.effectiveMini = false;
    if (!in.linesCleared && !(reverseAllSpin && in.spin && in.b2b >= 5)) return out;
    const bool voidClear = qp && reverseAllSpin && !in.spin;
    const int base = spin_base_attack(voidClear ? 0 : qp && in.spin && !out.effectiveMini
        ? 2 * in.linesCleared : basic_attack(in.linesCleared, in.spin && !out.effectiveMini), in);
    const double bonus = configured_b2b_bonus(in);
    const int combo = std::max(in.combo, 1) - 1;
    double raw = (base + bonus) * (1 + combo * 0.25);
    if (base + bonus == 0 && !voidClear) {
        raw = std::log(1.25 * combo + 1.0);
        if (Runtime::reverse(in.qp.snakeEyes)) raw *= 0.5;
    }
    if (qp) {
        Runtime::QuickPlayConfig mods{in.qp.expert, in.qp.noHold, in.qp.messy, in.qp.gravity,
            in.qp.volatileMod, in.qp.doubleHole, in.qp.invisible, in.qp.allSpin, in.qp.backfire, in.qp.snakeEyes};
        mods.snowball = in.qp.snowball;
        if (in.linesCleared == 1 && !in.spin && in.combo == 1 && !qp_hard_mode(mods)) ++raw;
    }
    const int charge = in.attackRules.b2bCharging ? std::max(0, in.b2bCharging) : 0;
    // Non-QP clear rewards and surge retain their independent rounding. QP
    // rolls once for the complete raw attack, as in historical recordings.
    const long long rounded = qp ? round_attack(raw + clearBonus + charge, in) :
        static_cast<long long>(round_attack(raw, in)) + out.perfectClearAttack +
        out.halfClearAttack + round_attack(charge, in);
    const int garbageBonus = in.clearedGarbageLines > 0 &&
        (in.spin || (in.linesCleared >= 4 && !reverseAllSpin))
        ? std::max(0, in.attackRules.garbageSpecialAttack) : 0;
    out.attack = static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(),
        rounded + garbageBonus));
    return out;
}

} // namespace

AttackResult calculate_attack(const AttackInput& in) {
    if (!in.attackRules.legacy) return configured_attack(in);
    AttackResult out;
    out.effectiveMini = in.mini;
    out.perfectClearAttack = round_attack(in.perfectClear ?
        (in.garbageMode == Runtime::GarbageMode::QuickPlay ? 3 : 5) : 0, in);
    const bool b2bIncrement = in.b2bTransition == B2BTransition::Increment;

    if (in.garbageMode == Runtime::GarbageMode::QuickPlay) {
        if (Runtime::reverse(in.qp.noHold)) out.effectiveMini = in.spin;
        else if (Runtime::enabled(in.qp.allSpin) && !standard_spin_piece(in.piece)) out.effectiveMini = false;
        const bool reverseAllSpin = in.qp.allSpin == Runtime::AllSpinMode::Reverse;
        if (!in.linesCleared && !(reverseAllSpin && in.spin && in.b2b >= 5)) return out;
        const bool voidClear = reverseAllSpin && !in.spin;
        const int basic = spin_base_attack(voidClear ? 0 : in.spin && !out.effectiveMini ? 2 * in.linesCleared
            : basic_attack(in.linesCleared, false), in);
        const int bonus = b2bIncrement && in.b2b >= 2 ? (reverseAllSpin && in.b2b >= 5 ? 2 : 1) : 0;
        const int combo = std::max(in.combo, 1) - 1;
        double raw = (basic + bonus) * (1 + combo * 0.25);
        if (basic + bonus == 0 && !voidClear) {
            raw = std::log(1.25 * combo + 1.0);
            if (Runtime::reverse(in.qp.snakeEyes)) raw *= 0.5;
        }
        Runtime::QuickPlayConfig mods{in.qp.expert, in.qp.noHold, in.qp.messy, in.qp.gravity,
            in.qp.volatileMod, in.qp.doubleHole, in.qp.invisible, in.qp.allSpin, in.qp.backfire, in.qp.snakeEyes};
        mods.snowball = in.qp.snowball;
        if (in.linesCleared == 1 && !in.spin && in.combo == 1 && !qp_hard_mode(mods)) ++raw;
        raw += (in.perfectClear ? 3 : 0) + std::max(0, in.b2bCharging - (reverseAllSpin ? 0 : 3));
        out.attack = round_attack(raw, in);
        return out;
    }

    if (in.qp.allSpin == Runtime::AllSpinMode::Reverse) {
        if (in.linesCleared || (in.spin && in.b2b >= 5)) {
            int b2bLevel = 0;
            if (!standard_spin_piece(in.piece)) out.effectiveMini = false;
            const int basic = spin_base_attack(basic_attack(
                in.linesCleared, in.spin && !out.effectiveMini), in);
            if (in.b2b >= 2) b2bLevel = 1;
            if (in.b2b >= 5) b2bLevel = 2;

            out.attack = round_attack(
                (basic + b2bLevel) *
                    (1 + (std::max(in.combo, 1) - 1) * 0.25),
                in);
            if (!basic && !b2bLevel) {
                out.attack = combo_single_attack(in, false);
            }
            out.attack += out.perfectClearAttack + round_attack(in.b2bCharging, in) +
                garbage_special_bonus(in);
        }
        return out;
    }

    if (!in.linesCleared) return out;

    int b2bLevel = 0;
    const int basic = spin_base_attack(basic_attack(
        in.linesCleared, in.spin && !out.effectiveMini), in);
    if (in.b2b >= 2) b2bLevel = 1;

    out.attack = round_attack(
        (basic + b2bLevel) *
            (1 + (std::max(in.combo, 1) - 1) * 0.25),
        in);
    if (!basic && !b2bLevel) {
        out.attack = combo_single_attack(
            in, Runtime::reverse(in.qp.snakeEyes));
    }

    const bool cleanQPOpenerSingle =
        in.garbageMode == Runtime::GarbageMode::QuickPlay && in.previousB2B == 0 &&
        in.linesCleared == 1 && !b2bIncrement && in.combo == 1 &&
        in.qp.expert == Runtime::ModifierMode::Disabled && !Runtime::reverse(in.qp.noHold) &&
        !Runtime::reverse(in.qp.messy) && !Runtime::reverse(in.qp.gravity) &&
        !Runtime::reverse(in.qp.volatileMod) && !Runtime::reverse(in.qp.doubleHole) &&
        !Runtime::reverse(in.qp.invisible) && in.qp.allSpin != Runtime::AllSpinMode::Reverse &&
        !Runtime::reverse(in.qp.backfire) && !Runtime::reverse(in.qp.snakeEyes);
    if (cleanQPOpenerSingle) ++out.attack;

    out.attack += out.perfectClearAttack +
        round_attack(std::max(0, in.b2bCharging - (in.garbageMode == Runtime::GarbageMode::QuickPlay ? 3 : 0)), in) +
        garbage_special_bonus(in);
    return out;
}

B2BReleaseGarbage split_b2b_release_garbage(
    int outgoingAttack,
    int storedCharge) {
    B2BReleaseGarbage out;
    const int outgoing = std::max(0, outgoingAttack);
    const int charge = std::min(outgoing, std::max(0, storedCharge));
    out.ordinaryAttack = outgoing - charge;

    const int quotient = charge / 3;
    const int remainder = charge % 3;
    out.chargePackets[0] = quotient + (remainder == 2);
    out.chargePackets[1] = quotient + (remainder == 2);
    out.chargePackets[2] = quotient + (remainder == 1);
    return out;
}

} // namespace Rules
