#ifndef TETR_AUTOPLAY_AVAILABILITY_H
#define TETR_AUTOPLAY_AVAILABILITY_H

#include "tetr/runtime/game_modes.h"
#include <string>

namespace Runtime { struct RuntimeConfig; }
namespace Autoplay {
struct Availability {
    bool usable = true;
    std::string reason;
    explicit operator bool() const noexcept { return usable; }
};

// Pure capability check: no allocation of a BOT engine or mutation of BOT state.
Availability availability(const Runtime::RuntimeConfig& config, Runtime::AutoplayMode mode);
} // namespace Autoplay
#endif
