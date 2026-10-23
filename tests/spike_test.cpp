#include "tetr/rules/spike.h"
#include "tetr/runtime/player_session_state.h"

#include <cassert>
#include <limits>

namespace TetrTests::spike_test {
namespace {

Runtime::TimePoint at(double seconds) {
    return Runtime::TimePoint::from_seconds(seconds);
}

} // namespace

int run(int argc,char** argv) {
    Runtime::SpikeDisplayState state;

    Rules::add_spike_attack(state, 4, at(10.0));
    assert(state.attack == 4);
    assert(!Rules::spike_visible(state));

    Rules::add_spike_attack(state, 4, at(10.4));
    assert(state.attack == 8);
    assert(Rules::spike_visible(state));

    Rules::add_spike_attack(state, 1, at(10.999));
    assert(state.attack == 9);
    assert(Rules::spike_visible(state));
    assert(!Rules::expire_spike(state, at(11.998)));
    assert(Rules::expire_spike(state, at(12.0)));
    assert(state.attack == 0);
    assert(!state.lastAttackAt);

    // Exactly one second starts a new spike instead of extending the old one.
    Rules::add_spike_attack(state, 7, at(20.0));
    Rules::add_spike_attack(state, 9, at(21.0));
    assert(state.attack == 9);
    assert(Rules::spike_visible(state));

    // Non-positive attack neither changes the value nor extends the timeout.
    Rules::add_spike_attack(state, 0, at(21.5));
    assert(Rules::expire_spike(state, at(22.0)));

    // Long bursts saturate instead of overflowing into a hidden value.
    Rules::add_spike_attack(state, std::numeric_limits<int>::max(), at(30.0));
    Rules::add_spike_attack(state, 1, at(30.5));
    assert(state.attack == std::numeric_limits<int>::max());
    assert(Rules::spike_visible(state));

    return 0;
}
} // namespace TetrTests::spike_test
