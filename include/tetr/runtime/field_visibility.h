#ifndef TETR_RUNTIME_FIELD_VISIBILITY_H
#define TETR_RUNTIME_FIELD_VISIBILITY_H

#include "tetr/runtime/time.h"
#include <cmath>

namespace Runtime {

inline bool periodic_invisible_flash(Duration elapsed,Duration interval,Duration duration) noexcept {
    if(duration<=Duration{} || elapsed<Duration{}) return false;
    if(interval<=Duration{}) return true;
    return elapsed>=interval && std::fmod(elapsed.seconds(),interval.seconds())<duration.seconds();
}

} // namespace Runtime
#endif
