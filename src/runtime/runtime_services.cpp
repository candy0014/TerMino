#include "tetr/runtime/runtime_services.h"

#include <chrono>

namespace Runtime {

RuntimeServices::RuntimeServices()
    : random(static_cast<unsigned int>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

} // namespace Runtime
