#include "tetr/rules/chain.h"
#include <algorithm>
#include <limits>

namespace Rules {

ChainResult update_chain(const ChainInput& in) {
    ChainResult out;
    out.previousB2B = in.currentB2B;
    if (!in.attackRules.legacy) {
        const bool pc = in.perfectClear && in.attackRules.perfectClearEnabled;
        const bool hc = !in.perfectClear && in.halfClear && in.attackRules.halfClearEnabled;
        const bool reverse = in.allSpinMod == Runtime::AllSpinMode::Reverse;
        const bool special = in.spin || (!reverse && in.linesCleared >= 4);
        out.b2b = std::max(0, in.currentB2B);
        if (in.linesCleared == 0) {
            out.b2bTransition = reverse && in.spin && in.currentB2B >= 5
                ? B2BTransition::Increment : B2BTransition::Preserve;
        } else out.b2bTransition = special || pc || hc ? B2BTransition::Increment : B2BTransition::Break;
        if (out.b2bTransition == B2BTransition::Break) {
            const int displayed = std::max(0, out.b2b - 1);
            if (in.attackRules.b2bCharging && out.b2b > 0 && displayed >= in.attackRules.b2bChargeThreshold)
                out.b2bCharging = static_cast<int>(std::clamp<long long>(
                    static_cast<long long>(displayed) - in.attackRules.b2bChargeReduction,
                    0, std::numeric_limits<int>::max()));
            out.b2b = 0;
        } else if (out.b2bTransition == B2BTransition::Increment) {
            const int increment = std::max(0, pc ? in.attackRules.perfectClearCharge : hc ? in.attackRules.halfClearCharge : 1);
            out.b2b += std::min(increment, std::numeric_limits<int>::max() - out.b2b);
        }
        out.combo = in.linesCleared ? std::min(in.currentCombo, std::numeric_limits<int>::max()-1)+1 : 0;
        return out;
    }

    if (in.allSpinMod == Runtime::AllSpinMode::Reverse) {
        if (in.linesCleared == 0) {
            out.b2bTransition = (in.spin && in.currentB2B >= 5)
                ? B2BTransition::Increment
                : B2BTransition::Preserve;
        } else {
            out.b2bTransition = (in.spin || in.perfectClear)
                ? B2BTransition::Increment
                : B2BTransition::Break;
        }
    } else {
        if (in.linesCleared == 0) {
            // A zero-line spin kept the old integer b2b_flag at 3 (2|1),
            // which preserved B2B rather than incrementing it.
            out.b2bTransition = B2BTransition::Preserve;
        } else if (in.linesCleared >= 4 || in.spin || in.perfectClear) {
            out.b2bTransition = B2BTransition::Increment;
        } else {
            out.b2bTransition = B2BTransition::Break;
        }
    }

    out.b2b = in.currentB2B;
    if (out.b2bTransition == B2BTransition::Break) {
        if (in.currentB2B >= 5) out.b2bCharging = in.currentB2B - 1;
        out.b2b = 0;
    } else if (out.b2bTransition == B2BTransition::Increment) {
        if (in.garbageMode == Runtime::GarbageMode::QuickPlay) {
            const bool ordinarySpecial = in.spin ||
                (in.allSpinMod != Runtime::AllSpinMode::Reverse && in.linesCleared >= 4);
            out.b2b += static_cast<int>(ordinarySpecial) + (in.perfectClear ? 2 : 0);
        } else ++out.b2b;
    }

    out.combo = in.linesCleared ? in.currentCombo + 1 : 0;
    return out;
}

} // namespace Rules
