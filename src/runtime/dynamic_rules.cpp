#include "tetr/runtime/dynamic_rules.h"

#include "tetr/runtime/game_context.h"

namespace Runtime {

GravityInterval current_gravity_interval(const GameContext& context) {
    return context.session.dynamicRules.gravityInterval.value_or(context.config.handling.gravityInterval);
}

Duration current_epld(const GameContext& context) {
    return context.session.dynamicRules.epld.value_or(context.config.handling.epld);
}

double current_survival_apm(const GameContext& context) {
    return context.session.dynamicRules.survivalApm.value_or(context.config.garbage.survivalApm);
}

Duration current_incoming_garbage_delay(const GameContext& context) {
    return context.session.dynamicRules.incomingGarbageDelay.value_or(context.config.garbage.incomingDelay);
}

void set_current_gravity_interval(GameContext& context, GravityInterval value) {
    context.session.dynamicRules.gravityInterval = value;
}

void set_current_epld(GameContext& context, Duration value) {
    context.session.dynamicRules.epld = value;
}

void set_current_survival_apm(GameContext& context, double value) {
    context.session.dynamicRules.survivalApm = value;
}

void set_current_incoming_garbage_delay(GameContext& context, Duration value) {
    context.session.dynamicRules.incomingGarbageDelay = value;
}

void reset_dynamic_rules(GameContext& context) {
    context.session.dynamicRules.clear();
}

} // namespace Runtime
