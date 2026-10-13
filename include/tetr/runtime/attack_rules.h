#ifndef TETR_RUNTIME_ATTACK_RULES_H
#define TETR_RUNTIME_ATTACK_RULES_H

namespace Runtime {

enum class B2BReward { FixedAttack, Chaining, None };

struct AttackRules {
    B2BReward b2bReward=B2BReward::FixedAttack;
    int b2bRewardAmount=1;
    bool b2bCharging=true;
    // Counts in this configuration use the displayed B2B (streak minus one).
    int b2bChargeThreshold=4;
    int b2bChargeReduction=0;
    bool perfectClearEnabled=true;
    int perfectClearAttack=5;
    int perfectClearCharge=1;
    bool halfClearEnabled=false;
    int halfClearAttack=0;
    int halfClearCharge=0;
    int garbageSpecialAttack=1;
    // Recordings without AttackRulesVersion retain the historical QP rules.
    bool legacy=false;
};

} // namespace Runtime

#endif
