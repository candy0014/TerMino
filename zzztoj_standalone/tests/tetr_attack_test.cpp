#include "tetr_attack.h"

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    using TetrAttack::Input;
    using TetrAttack::State;

    auto result = TetrAttack::calculate({}, Input{1, false, false, false});
    require(result.attack == 0 && result.state.combo == 1 && result.state.b2b == 0,
            "fresh single");
    result = TetrAttack::calculate(result.state, Input{1, false, false, false});
    require(result.attack == 0 && result.state.combo == 2, "second single");
    result = TetrAttack::calculate(result.state, Input{1, false, false, false});
    require(result.attack == 1 && result.state.combo == 3, "third single combo");

    result = TetrAttack::calculate({}, Input{2, false, false, false});
    require(result.attack == 1, "double");
    result = TetrAttack::calculate({}, Input{3, false, false, false});
    require(result.attack == 2, "triple");
    result = TetrAttack::calculate({}, Input{4, false, false, false});
    require(result.attack == 4 && result.state.b2b == 1, "first tetris");
    result = TetrAttack::calculate(result.state, Input{4, false, false, false});
    require(result.attack == 6 && result.state.b2b == 2, "back-to-back tetris");

    result = TetrAttack::calculate({}, Input{1, true, false, false});
    require(result.attack == 2 && result.state.b2b == 1, "T-spin single");
    result = TetrAttack::calculate(result.state, Input{2, true, false, false});
    require(result.attack == 6 && result.state.b2b == 2, "B2B T-spin double");
    result = TetrAttack::calculate({}, Input{1, true, true, false});
    require(result.attack == 0 && result.state.b2b == 1, "mini T-spin single");
    result = TetrAttack::calculate({}, Input{2, true, true, false});
    require(result.attack == 1 && result.state.b2b == 1, "mini spin double");
    result = TetrAttack::calculate({}, Input{4, true, false, false});
    require(result.attack == 10, "full spin quad");

    result = TetrAttack::calculate(State{0, 42}, Input{1, false, false, false});
    require(result.attack == 3, "42-combo logarithmic boundary");
    result = TetrAttack::calculate(State{0, 43}, Input{1, false, false, false});
    require(result.attack == 4, "43-combo logarithmic boundary");

    result = TetrAttack::calculate({}, Input{1, false, false, true});
    require(result.attack == 5 && result.state.b2b == 1, "perfect-clear single");
    result = TetrAttack::calculate(State{5, 0}, Input{1, false, false, false});
    require(result.attack == 4 && result.b2bCharging == 4 && result.state.b2b == 0,
            "long-B2B charging payout");
    result = TetrAttack::calculate(State{3, 7}, Input{0, true, false, false});
    require(result.attack == 0 && result.state.b2b == 3 && result.state.combo == 0,
            "zero-line spin preserves B2B");
    result = TetrAttack::calculate(State{3, 0}, Input{2, true, false, true});
    require(result.state.b2b == 4, "spin perfect clear increments B2B once");
    return 0;
}
