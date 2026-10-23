#include "tetr/engine/input_precharge.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/input/services.h"
#include "tetr/runtime/game_context.h"

#include <algorithm>

namespace Engine {
namespace {
void precharge(Runtime::GameContext& context, MovementInputState& input,
    Runtime::TimePoint now, bool pausedWallTime) {
    if (context.config.mode.replayMode || Runtime::autoplay_enabled(context.config.autoplay.mode) ||
        !context.input_component()) return;
    auto& services=context.input_services();
    const auto event_time=[&](std::chrono::steady_clock::time_point time) {
        if (!pausedWallTime) return context.real_time_at(time);
        const auto stamp=context.services.clock.at(time);
        // Events from earlier gameplay exclude its completed pauses; events
        // inside this pause keep their actual press/release durations.
        return stamp>=context.pause.started ? stamp
            : context.real_time_at(time)+context.pause.removedTime;
    };
    const auto observe=[&](int key,bool down,Runtime::TimePoint time) {
        const auto action=services.bindings.action_for(key);
        if (!context.config.movement.allows_horizontal(Input::horizontal_delta(action))) {
            input.setPhase(key,KeyPhase::Released); return;
        }
        if (!input.consumeRelease(key,down)) input.beginPress(key,down,time);
    };
    if (services.gameplay_input_active()) {
        // Seed already-held keys before consuming newer events. Their real
        // press order matters when both directions are held at game start.
        std::vector<Input::KeyEvent> held;
        for (const auto& [key,action]:services.bindings.entries()) {
            if (!Input::is_horizontal(action)) continue;
            if (!services.gameplay_key_down(key)) observe(key,false,now);
            else held.push_back({key,true,services.gameplay_press_time(key).value_or(std::chrono::steady_clock::now()),0});
        }
        std::stable_sort(held.begin(),held.end(),[](const auto& a,const auto& b) { return a.time<b.time; });
        const auto events=services.take_spawn_horizontal_events();
        if (const auto sampled=services.gameplay_sample_time()) now=std::min(now,event_time(*sampled));
        // `now` may predate the events when polling/navigation descheduled
        // this thread. Preserve real press times; only timer advancement is
        // limited by the completed sample, never the origin of a new hold.
        for (const auto& event:held) observe(event.key,true,event_time(event.time));
        for (const auto& event:events) {
            observe(event.key,event.down,event_time(event.time));
        }
    } else {
        for (const auto& [key,action]:services.bindings.entries()) {
            if (Input::is_horizontal(action)) observe(key,services.key_down(key),now);
        }
    }
    for (const auto& [key,action]:services.bindings.entries()) {
        if (Input::is_horizontal(action))
            input.prechargeHorizontal(key,now,context.config.handling.das,context.config.handling.arr,
                context.config.handling.dasPrecharge);
    }
}
}

void precharge_movement_input(Runtime::GameContext& context, MovementInputState& input) {
    precharge(context,input,context.real_now(),false);
}

void precharge_paused_movement_input(Runtime::GameContext& context,
    MovementInputState& input, Runtime::TimePoint wallNow) {
    if (!context.config.handling.dasPrecharge) return;
    precharge(context,input,wallNow,true);
}

void finish_start_precharge(Runtime::GameContext& context, MovementInputState& input) {
    if (context.config.mode.replayMode || !context.input_component()) return;
    precharge_movement_input(context,input);
    // Prestart movement carries into the first piece; other countdown keys
    // retain release debounce rather than becoming queued gameplay actions.
    if (context.input_services().gameplay_input_active()) context.input_services().discard_prestart_actions();
}
}
