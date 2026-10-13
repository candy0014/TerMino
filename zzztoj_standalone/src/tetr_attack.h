#ifndef ZZZTOJ_STANDALONE_TETR_ATTACK_H
#define ZZZTOJ_STANDALONE_TETR_ATTACK_H

namespace TetrAttack {

enum class B2BTransition {
    Break,
    Increment,
    Preserve
};

struct State {
    int b2b = 0;
    int combo = 0;
};

struct Input {
    int linesCleared = 0;
    bool spin = false;
    bool mini = false;
    bool perfectClear = false;
};

struct Result {
    int attack = 0;
    State state{};
    int previousB2B = 0;
    int b2bCharging = 0;
    B2BTransition transition = B2BTransition::Break;
};

// Current TETR.IO multiplayer/Tetra League attack calculation, as documented
// by tetriswiki.cn/p/TETR.IO (revision 38236): Multiplier combo, All-Mini+,
// floor rounding, +5 all clear, and Tetra League B2B Charging. Piece-spin
// classification happens before this function. Quick Play's stochastic
// rounding and its different all-clear/surge parameters are intentionally not
// used by this deterministic single-player visualizer.
Result calculate(State current, const Input& input);

} // namespace TetrAttack

#endif
