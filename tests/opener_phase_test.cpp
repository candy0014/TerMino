#include "tetr/rules/opener_phase.h"
#include <cassert>

int main() {
    using Rules::opener_phase_double_cancel;
    using Rules::opener_phase_cancel_budget;

    assert(opener_phase_double_cancel(1, 4, 6, true));
    assert(opener_phase_cancel_budget(4, true) == 8);

    assert(opener_phase_double_cancel(14, 4, 10, true));
    assert(!opener_phase_double_cancel(15, 4, 10, true));
    assert(!opener_phase_double_cancel(14, 4, 4, true));
    assert(!opener_phase_double_cancel(14, 4, 3, true));
    assert(!opener_phase_double_cancel(14, 4, 10, false));
    assert(!opener_phase_double_cancel(14, 0, 10, true));

    return 0;
}
