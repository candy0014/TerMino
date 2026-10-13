#include "tetr/runtime/monotonic_clock.h"
#include <cstdlib>
#include <cmath>
#include <thread>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __linux__
#include <unistd.h>
#endif

namespace Runtime {

MonotonicClock::MonotonicClock():start_(std::chrono::steady_clock::now()){
    if (const char* value = std::getenv("TETR_TEST_CLOCK_SCALE")) {
        char* end = nullptr;
        const double parsed = std::strtod(value, &end);
        if (end != value && *end == '\0' && std::isfinite(parsed) && parsed > 0.0)
            scale_ = parsed;
    }
}

TimePoint MonotonicClock::now() const{
    return at(std::chrono::steady_clock::now());
}

TimePoint MonotonicClock::at(std::chrono::steady_clock::time_point time) const{
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(time-start_);
    const double seconds=static_cast<double>(elapsed.count())*
        std::chrono::microseconds::period::num/std::chrono::microseconds::period::den;
    return TimePoint::from_seconds(seconds * scale_);
}

void MonotonicClock::sleep_for(Duration duration) const{
    const double seconds=duration.seconds() / scale_;
    if (waitInterrupt) waitInterrupt();
    if(seconds<=0.0) return;
    if (waitInterrupt) {
        const auto end = std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
        do {
            std::this_thread::sleep_until(std::min(end,
                std::chrono::steady_clock::now() + std::chrono::milliseconds(10)));
            waitInterrupt();
        } while (std::chrono::steady_clock::now() < end);
        return;
    }
#ifdef _WIN32
    Sleep(static_cast<DWORD>(seconds*1000.0));
#endif
#ifdef __linux__
    usleep(static_cast<useconds_t>(seconds*1000000.0));
#endif
}

std::chrono::microseconds MonotonicClock::wall_duration(Duration duration) const {
    return std::chrono::microseconds(static_cast<long long>(duration.seconds()/scale_*1000000.0));
}

} // namespace Runtime
