#ifndef TETR_RUNTIME_GRAVITY_H
#define TETR_RUNTIME_GRAVITY_H

#include "tetr/runtime/time.h"

namespace Runtime {

// Natural gravity is a three-state rule: disabled, instant, or periodic.
// Historical code encoded disabled as 1e9+5 seconds and instant as ~0; those
// numeric sentinels are now confined to explicit compatibility conversions.
class GravityInterval {
public:
    static constexpr GravityInterval disabled() noexcept {
        return GravityInterval(false, Duration{});
    }
    static constexpr GravityInterval instant() noexcept {
        return GravityInterval(true, Duration{});
    }
    static constexpr GravityInterval periodic(Duration interval) noexcept {
        return GravityInterval(true, interval);
    }
    constexpr bool enabled() const noexcept { return enabled_; }
    constexpr bool is_disabled() const noexcept { return !enabled_; }
    constexpr bool is_instant() const noexcept { return enabled_ && interval_ == Duration{}; }
    constexpr Duration interval() const noexcept { return interval_; }

    // Historical acceleration treated disabled gravity as an enormous finite
    // period. Keep that compatibility seed encapsulated here instead of
    // leaking its sentinel value into Rules/Engine.
    constexpr Duration acceleration_seed_period() const noexcept {
        return enabled_ ? interval_ : Duration::from_seconds(kDisabledCompatibilitySeconds);
    }

    friend constexpr bool operator==(GravityInterval lhs, GravityInterval rhs) noexcept {
        return lhs.enabled_ == rhs.enabled_ && (!lhs.enabled_ || lhs.interval_ == rhs.interval_);
    }
    friend constexpr bool operator!=(GravityInterval lhs, GravityInterval rhs) noexcept { return !(lhs == rhs); }

private:
    static constexpr double kDisabledCompatibilitySeconds = 1e9 + 5.0;

    constexpr GravityInterval(bool enabled, Duration interval) noexcept
        : enabled_(enabled), interval_(interval) {}

    bool enabled_ = true;
    Duration interval_{};
};

} // namespace Runtime

#endif
