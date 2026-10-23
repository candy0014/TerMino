#ifndef TETR_RULES_QP_POST_LOCK_H
#define TETR_RULES_QP_POST_LOCK_H

#include <optional>
#include <vector>

#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/strong_types.h"
#include "tetr/runtime/quick_play_operation.h"

namespace Rules {

using QPClearOperation = Runtime::QuickPlayClearOperation;

bool qp_has_clear_operation(const QPClearOperation& operation);

struct DoubleHoleReverseInput {
    bool enabled = false;
    int attack = 0;
    int linesCleared = 0;
    int clearedGarbageLines = 0;
    bool blighted = false;
    double roundingRoll = 1.0;
};

struct DoubleHoleReverseResult {
    int attack = 0;
    bool blighted = false;
};

DoubleHoleReverseResult apply_double_hole_reverse(const DoubleHoleReverseInput& input);

struct SnakeEyesResult {
    int attack = 0;
    int selfGarbage = 0;
};

SnakeEyesResult apply_snake_eyes(Runtime::ModifierMode mode, int linesCleared, int attack);

int qp_offset_attack(int attack, Runtime::ModifierMode backfireMode, Runtime::ModifierMode volatileMode);
int qp_outgoing_after_offset(int offsetResult, int attack, Runtime::ModifierMode backfireMode, Runtime::ModifierMode volatileMode);
int qp_backfire_self_garbage(Runtime::ModifierMode backfireMode, int outgoing);

struct AllSpinRepeatResult {
    std::optional<QPClearOperation> operation;
    int solidGarbage = 0;
    bool decayTimedSolidGarbage = false;
};

AllSpinRepeatResult evaluate_all_spin_repeat(
    Runtime::AllSpinMode allSpinMode,
    const std::optional<QPClearOperation>& previousOperation,
    const QPClearOperation& operation);

Runtime::ExtendedAllSpinSignature extended_all_spin_signature(const QPClearOperation& operation);

// Mutates the history exactly as the old QP All-Spin extension did and returns
// the multiplicative speed penalty (1.0 when the operation is new).
double update_extended_all_spin_history(
    std::vector<Runtime::ExtendedAllSpinSignature>& history,
    Runtime::ExtendedAllSpinSignature signature);

} // namespace Rules

#endif
