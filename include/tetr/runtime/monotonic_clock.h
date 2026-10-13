#ifndef TETR_RUNTIME_MONOTONIC_CLOCK_H
#define TETR_RUNTIME_MONOTONIC_CLOCK_H

#include "tetr/runtime/time.h"

#include <chrono>
#include <functional>

namespace Runtime {

// Process-lifetime monotonic clock. All callers use semantic TimePoint / Duration.
class MonotonicClock {
public:
    MonotonicClock();
    ~MonotonicClock() = default;

    TimePoint now() const;
    TimePoint at(std::chrono::steady_clock::time_point time) const;
    std::chrono::microseconds wall_duration(Duration duration) const;
    void sleep_for(Duration duration) const;
    std::function<void()> waitInterrupt;

private:
    std::chrono::steady_clock::time_point start_;
    double scale_ = 1.0;
};

} // namespace Runtime

#endif
