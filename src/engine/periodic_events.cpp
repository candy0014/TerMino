#include "tetr/engine/periodic_events.h"

#include <cmath>

namespace Engine {

bool qp_invisible_flash(Runtime::Duration elapsedGameTime) noexcept {
    return elapsedGameTime.seconds()>=5 && std::fmod(elapsedGameTime.seconds(),5.0)<1.0;
}

bool periodic_event_due(Runtime::TimePoint now, Runtime::TimePoint last, Runtime::Duration interval) {
    return now - last > interval;
}

Runtime::GravityInterval advance_qp_gravity_speed(Runtime::GravityInterval gravity, bool enforceUpperGuard) {
    if (gravity.is_disabled() && enforceUpperGuard) return gravity;
    double value = gravity.acceleration_seed_period().seconds();
    if (value > 1e-8) value = 1.0 / (1.0 / value + 0.03);
    if (value * 1200.0 < 1.0) return Runtime::GravityInterval::instant();
    return Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(value));
}

bool autoplay_step_due(bool heldPieceContinuation, double pps, Runtime::TimePoint now, Runtime::TimePoint lastStep) {
    if (heldPieceContinuation || std::fabs(pps) < 1e-8) return true;
    return now - lastStep > Runtime::Duration::from_seconds(1.0 / pps);
}

bool blitz_time_expired(
    Runtime::GameMode mode,
    Runtime::Duration elapsedGameTime,
    Runtime::Duration blitzTime) {
    return mode == Runtime::GameMode::Blitz && elapsedGameTime > blitzTime;
}

} // namespace Engine
