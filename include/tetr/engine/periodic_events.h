#ifndef TETR_ENGINE_PERIODIC_EVENTS_H
#define TETR_ENGINE_PERIODIC_EVENTS_H

#include "tetr/runtime/gravity.h"
#include "tetr/runtime/time.h"
#include "tetr/runtime/game_modes.h"

namespace Engine {

bool periodic_event_due(Runtime::TimePoint now, Runtime::TimePoint last, Runtime::Duration interval);
bool qp_invisible_flash(Runtime::Duration elapsedGameTime) noexcept;

// Preserves the legacy QP one-second gravity acceleration formula. Disabled
// gravity stays disabled in live play; replay can explicitly request the old
// no-upper-guard transition for historical recordings.
Runtime::GravityInterval advance_qp_gravity_speed(Runtime::GravityInterval gravity, bool enforceUpperGuard);

bool autoplay_step_due(bool heldPieceContinuation, double pps, Runtime::TimePoint now, Runtime::TimePoint lastStep);

bool blitz_time_expired(Runtime::GameMode mode, Runtime::Duration elapsedGameTime, Runtime::Duration blitzTime);

} // namespace Engine

#endif
