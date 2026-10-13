#ifndef TETR_COMPAT_GARBAGE_ENCODING_H
#define TETR_COMPAT_GARBAGE_ENCODING_H

#include "tetr/runtime/garbage_hole_pattern.h"

namespace Compat {

constexpr Runtime::GarbageHolePattern hole_pattern_from_legacy(int value) noexcept {
    return value < 0
        ? Runtime::GarbageHolePattern::contiguous(-value)
        : Runtime::GarbageHolePattern::scattered(value);
}
constexpr int to_legacy(Runtime::GarbageHolePattern value) noexcept {
    return value.is_contiguous() ? -value.extent() : value.extent();
}

} // namespace Compat

#endif
