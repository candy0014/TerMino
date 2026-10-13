#ifndef TETR_RUNTIME_RUNTIME_SERVICES_H
#define TETR_RUNTIME_RUNTIME_SERVICES_H

#include "tetr/runtime/monotonic_clock.h"

#include <random>

namespace Runtime {

// Process-lifetime services owned by the simulation/runtime layer itself.
// Input, user configuration, networking and replay are injected separately at
// the application composition root.
struct RuntimeServices {
    MonotonicClock clock;
    std::mt19937 random;

    RuntimeServices();
};

} // namespace Runtime

#endif
