#include "tetr_attack.h"

#include <algorithm>
#include <cmath>

namespace TetrAttack {
namespace {

int basic_attack(const Input& input) {
    if (input.linesCleared <= 0) return 0;

    // Current TETR.IO base table:
    //   normal / Mini: 0, 1, 2, 4
    //   full Spin:     2, 4, 6, 10
    // The formulae below also cover the game's possible 5+ line clears.
    if (input.spin && !input.mini) {
        return input.linesCleared >= 4
            ? 2 * (input.linesCleared + 1)
            : 2 * input.linesCleared;
    }
    if (input.linesCleared == 1) return 0;
    if (input.linesCleared == 2) return 1;
    if (input.linesCleared == 3) return 2;
    return input.linesCleared;
}

} // namespace

Result calculate(State current, const Input& input) {
    Result out;
    out.previousB2B = current.b2b;

    // TETR.IO difficult clears are Quads, Spins that clear a line, and all
    // clears. An all clear only increments once even when it is also a Spin.
    if (input.linesCleared == 0) {
        out.transition = B2BTransition::Preserve;
    } else if (input.linesCleared >= 4 || input.spin || input.perfectClear) {
        out.transition = B2BTransition::Increment;
    } else {
        out.transition = B2BTransition::Break;
    }

    out.state.b2b = current.b2b;
    if (out.transition == B2BTransition::Break) {
        if (current.b2b >= 5) out.b2bCharging = current.b2b - 1;
        out.state.b2b = 0;
    } else if (out.transition == B2BTransition::Increment) {
        ++out.state.b2b;
    }
    out.state.combo = input.linesCleared ? current.combo + 1 : 0;

    // Multiplier attack with the default floor rounding used by Tetra League
    // and most multiplayer modes. State::combo counts consecutive clears,
    // while the displayed/formula combo starts at zero.
    if (!input.linesCleared) return out;

    const int basic = basic_attack(input);
    const int b2bBonus = out.state.b2b >= 2 ? 1 : 0;
    const int combo = out.state.combo - 1;
    double rawAttack = (basic + b2bBonus) * (1.0 + 0.25 * combo);
    if (combo > 1) {
        rawAttack = std::max(rawAttack, std::log(1.25 * combo + 1.0));
    }
    out.attack = static_cast<int>(std::floor(rawAttack));
    out.attack += (input.perfectClear ? 5 : 0) + std::max(0, out.b2bCharging);
    return out;
}

} // namespace TetrAttack
