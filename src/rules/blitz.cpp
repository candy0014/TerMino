#include "tetr/rules/blitz.h"
#include "tetr/runtime/gravity_progression.h"

#include <algorithm>

namespace Rules {

int blitz_level_from_lines(int clearedLines) {
    return Runtime::increasing_gravity_level(clearedLines);
}

Runtime::Duration blitz_gravity_for_level(int level) {
    return Runtime::increasing_gravity_interval(level);
}

} // namespace Rules
