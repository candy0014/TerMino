#ifndef TETR_RULES_BLITZ_H
#define TETR_RULES_BLITZ_H

#include "tetr/runtime/time.h"

namespace Rules {

int blitz_level_from_lines(int clearedLines);
Runtime::Duration blitz_gravity_for_level(int level);

} // namespace Rules

#endif
