#ifndef TETR_COMPAT_GRAVITY_ENCODING_H
#define TETR_COMPAT_GRAVITY_ENCODING_H

#include "tetr/runtime/gravity.h"

namespace Compat {

constexpr double kDisabledGravityLegacySeconds = 1e9 + 5.0;
constexpr Runtime::GravityInterval gravity_from_legacy_seconds(double seconds) noexcept {
    return seconds > 1e9
        ? Runtime::GravityInterval::disabled()
        : (seconds <= 1e-7
            ? Runtime::GravityInterval::instant()
            : Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(seconds)));
}
constexpr double to_legacy_seconds(Runtime::GravityInterval gravity) noexcept {
    return gravity.is_disabled() ? kDisabledGravityLegacySeconds : gravity.interval().seconds();
}

} // namespace Compat

#endif
