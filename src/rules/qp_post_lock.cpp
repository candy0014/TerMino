#include "tetr/rules/qp_post_lock.h"

#include <cmath>

namespace Rules {
namespace {

bool same_non_spin_repeat_class(
    const QPClearOperation& lhs,
    const QPClearOperation& rhs) noexcept {
    // Historical `(encoded | 255)` ignored only the low eight piece-id bits.
    // QP piece ids occupy those bits, so express the rule directly instead of
    // reconstructing the old packed integer.
    return lhs.linesCleared == rhs.linesCleared &&
           lhs.spin == rhs.spin &&
           lhs.mini == rhs.mini;
}

} // namespace

bool qp_has_clear_operation(const QPClearOperation& op) {
    return op.linesCleared != 0 || op.spin;
}

DoubleHoleReverseResult apply_double_hole_reverse(const DoubleHoleReverseInput& in) {
    DoubleHoleReverseResult out{in.attack, in.blighted};
    if (!in.enabled) return out;
    if (in.clearedGarbageLines == 0) {
        if (in.linesCleared && out.blighted) {
            out.blighted = false;
            out.attack = out.attack * 7 / 4 + 1;
        } else out.attack = 0;
    } else {
        if (out.blighted) out.attack = out.attack * 7 / 4;
        out.blighted = true;
    }
    return out;
}

SnakeEyesResult apply_snake_eyes(Runtime::ModifierMode mode, int linesCleared, int attack) {
    SnakeEyesResult out{attack, 0};
    if (Runtime::reverse(mode) && linesCleared != 1 && attack) out.selfGarbage = attack;
    if (Runtime::enabled(mode) && linesCleared != 1) out.attack = 0;
    return out;
}

int qp_offset_attack(int attack, Runtime::ModifierMode backfireMode, Runtime::ModifierMode volatileMode) {
    const int shift = volatileMode == Runtime::ModifierMode::Enabled ? 1 : 0;
    return (Runtime::reverse(backfireMode) ? 0 : attack) << shift;
}
int qp_outgoing_after_offset(int offsetResult, int attack, Runtime::ModifierMode backfireMode, Runtime::ModifierMode volatileMode) {
    if (Runtime::reverse(backfireMode)) return attack;
    if (volatileMode == Runtime::ModifierMode::Enabled) return offsetResult >> 1;
    return offsetResult;
}
int qp_backfire_self_garbage(Runtime::ModifierMode backfireMode, int outgoing) {
    return Runtime::enabled(backfireMode) && outgoing >= 2 ? (outgoing >> 1) : 0;
}

AllSpinRepeatResult evaluate_all_spin_repeat(
    Runtime::AllSpinMode allSpinMode,
    const std::optional<QPClearOperation>& previousOperation,
    const QPClearOperation& op) {
    AllSpinRepeatResult out;
    out.operation = previousOperation;
    if (!qp_has_clear_operation(op)) return out;
    out.operation = op;

    if (allSpinMode == Runtime::AllSpinMode::Enabled) {
        if (previousOperation) {
            if (op == *previousOperation || (!op.spin && same_non_spin_repeat_class(op, *previousOperation))) {
                out.solidGarbage = 1;
            } else if (op.linesCleared) {
                out.decayTimedSolidGarbage = true;
            }
        } else if (op.linesCleared) {
            out.decayTimedSolidGarbage = true;
        }
    } else if (allSpinMode == Runtime::AllSpinMode::Reverse) {
        const int currentSpinLines = op.spin ? op.linesCleared : -1;
        // The historical packed "no previous operation" state decoded to
        // spin=true, lines=7.  Preserve that observable rule directly without
        // retaining the sentinel or packed representation in Rules.
        constexpr int kInitialReverseSpinLineClass = 7;
        const int previousSpinLines = previousOperation
            ? (previousOperation->spin ? previousOperation->linesCleared : -1)
            : kInitialReverseSpinLineClass;
        if (previousSpinLines == currentSpinLines) out.solidGarbage = 20;
    }
    return out;
}

Runtime::ExtendedAllSpinSignature extended_all_spin_signature(const QPClearOperation& op) {
    // The old Extended representation ORed piece-id bits with the line/spin/
    // mini fields.  Some April-Fools ids therefore influenced those classes.
    // Compute the exact comparison classes directly, without creating a packed
    // integer first.
    const int pieceId = op.pieceType.index();
    return Runtime::ExtendedAllSpinSignature{
        op.spin ? (pieceId & 127) : 127,
        ((pieceId >> 7) | op.linesCleared) & 7,
        ((pieceId >> 10) & 1) != 0 || op.spin,
        ((pieceId >> 11) & 1) != 0 || op.mini,
        (pieceId >> 12) | (op.linesCleared >> 5)
    };
}

double update_extended_all_spin_history(
    std::vector<Runtime::ExtendedAllSpinSignature>& history,
    Runtime::ExtendedAllSpinSignature signature) {
    int position = static_cast<int>(history.size());
    for (int i = 0; i < static_cast<int>(history.size()); ++i) {
        if (history[i] == signature) { position = i; break; }
    }
    if (position == static_cast<int>(history.size())) {
        history.insert(history.begin(), signature);
        return 1.0;
    }
    const double multiplier = std::pow(0.9, 1.0 / (position + 0.5));
    for (int i = position - 1; i >= 0; --i) history[i + 1] = history[i];
    history[0] = signature;
    return multiplier;
}

} // namespace Rules
