#ifndef TETR_RULES_CHAIN_H
#define TETR_RULES_CHAIN_H

#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/attack_rules.h"

namespace Rules {

enum class B2BTransition {
    Break,
    Increment,
    Preserve
};

struct ChainInput {
    int linesCleared = 0;
    bool spin = false;
    bool perfectClear = false;
    Runtime::AllSpinMode allSpinMod = Runtime::AllSpinMode::Disabled;
    int currentB2B = 0;
    int currentCombo = 0;
    Runtime::GarbageMode garbageMode = Runtime::GarbageMode::None;
    Runtime::AttackRules attackRules;
    bool halfClear = false;
    bool qpDisableB2B = false;
};

struct ChainResult {
    B2BTransition b2bTransition = B2BTransition::Break;
    int previousB2B = 0;
    int b2bCharging = 0;
    int b2b = 0;
    int combo = 0;
};

ChainResult update_chain(const ChainInput& input);

} // namespace Rules

#endif
