#ifndef TETR_RULES_ATTACK_H
#define TETR_RULES_ATTACK_H

#include <array>

#include "tetr/rules/chain.h"
#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/spin_bonuses.h"

namespace Rules {

struct PieceAttackTraits {
    int familyIndex = 4;
    Runtime::RotationSystem rotationSystem = Runtime::RotationSystem::SrsPlus;
};

struct QPAttackModifiers {
    Runtime::ModifierMode expert = Runtime::ModifierMode::Disabled;
    Runtime::ModifierMode noHold = Runtime::ModifierMode::Disabled;
    Runtime::ModifierMode messy = Runtime::ModifierMode::Disabled;
    Runtime::ModifierMode gravity = Runtime::ModifierMode::Disabled;
    Runtime::ModifierMode volatileMod = Runtime::ModifierMode::Disabled;
    Runtime::ModifierMode doubleHole = Runtime::ModifierMode::Disabled;
    Runtime::ModifierMode invisible = Runtime::ModifierMode::Disabled;
    Runtime::AllSpinMode allSpin = Runtime::AllSpinMode::Disabled;
    Runtime::ModifierMode backfire = Runtime::ModifierMode::Disabled;
    Runtime::ModifierMode snakeEyes = Runtime::ModifierMode::Disabled;
    Runtime::ModifierMode snowball = Runtime::ModifierMode::Disabled;
};

struct AttackInput {
    int linesCleared = 0;
    bool spin = false;
    bool mini = false;
    bool perfectClear = false;
    int b2b = 0;
    int combo = 0;
    int previousB2B = 0;
    int b2bCharging = 0;
    B2BTransition b2bTransition = B2BTransition::Break;
    int clearedGarbageLines = 0;
    // Uniform [0, 1) roll used only by QUICK PLAY's RNG rounding.  A value of
    // 1 keeps direct/spec callers deterministic and rounds down.
    double roundingRoll = 1.0;
    Runtime::GarbageMode garbageMode = Runtime::GarbageMode::None;
    PieceAttackTraits piece;
    QPAttackModifiers qp;
    Runtime::SpinBonuses spinBonuses = Runtime::SpinBonuses::AllMiniPlus;
    double garbageMultiplier = 1.0;
    Runtime::AttackRules attackRules;
    bool halfClear = false;
};

struct AttackResult {
    int attack = 0;
    bool effectiveMini = false;
    int perfectClearAttack = 0;
    int halfClearAttack = 0;
};

struct B2BReleaseGarbage {
    int ordinaryAttack = 0;
    std::array<int, 3> chargePackets{};
};

AttackResult calculate_attack(const AttackInput& input);

// TETR.IO Season 2 releases stored B2B charge as three garbage packets.  The
// first two packets are equal and no two packets differ by more than one line:
// 10 -> 3+3+4, 11 -> 4+4+3.  Any ordinary attack from the breaking clear stays
// separate.  Incoming cancellation is represented by outgoingAttack already;
// it consumes ordinary attack first and can then reduce the stored release.
B2BReleaseGarbage split_b2b_release_garbage(
    int outgoingAttack,
    int storedCharge);

} // namespace Rules

#endif
