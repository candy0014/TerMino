#ifndef TETR_RUNTIME_DYNAMIC_RULES_H
#define TETR_RUNTIME_DYNAMIC_RULES_H

#include "tetr/runtime/gravity.h"
#include "tetr/runtime/time.h"

namespace Runtime {

class GameContext;

GravityInterval current_gravity_interval(const GameContext& context);
Duration current_epld(const GameContext& context);
double current_survival_apm(const GameContext& context);
Duration current_incoming_garbage_delay(const GameContext& context);

void set_current_gravity_interval(GameContext& context, GravityInterval value);
void set_current_epld(GameContext& context, Duration value);
void set_current_survival_apm(GameContext& context, double value);
void set_current_incoming_garbage_delay(GameContext& context, Duration value);
void reset_dynamic_rules(GameContext& context);

} // namespace Runtime

#endif
