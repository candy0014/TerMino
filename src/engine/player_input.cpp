#include "tetr/engine/player_input.h"
#include "tetr/input/services.h"
#include <algorithm>

namespace Engine {
PlayerInputResult process_buffered_player_input_frame(
    MovementInputState& input, MovementController& movement,
    Input::Services& services, const std::vector<int>& rotateKeys,
    const std::vector<int>& moveKeys, const PlayerInputConfig& config,
    Runtime::TimePoint now,
    const std::function<Runtime::TimePoint(std::chrono::steady_clock::time_point)>& eventTime,
    const std::function<void()>& afterStep) {
    PlayerInputResult result;
    const auto step = [&](Runtime::TimePoint time) {
        const auto effect = process_player_input_frame(input, movement, services.bindings,
            rotateKeys, moveKeys, config,
            [&](int key) { return services.gameplay_key_down(key); }, [=] { return time; });
        result.scoreDelta += effect.scoreDelta;
        result.command = effect.command;
        if (afterStep) afterStep();
        return result.command != PlayerInputCommand::None;
    };
    services.poll_gameplay_events();
    while (const auto* event = services.next_gameplay_event()) {
        const auto time = std::max(input.prepared_at(), std::min(now, eventTime(event->time)));
        // Finish held-key repeats before a release or direction change.
        // A newly pressed action must participate before this frame's instant
        // fall. Releases and direction changes still finish the old repeats.
        const bool instantFall=config.gravityInterval.is_instant() ||
            (!config.movement.disableSoftDrop &&
                soft_drop_interval(config.gravityInterval,config.softDropFactor,config.softDropFixed)==Runtime::Duration{} &&
                std::any_of(services.bindings.entries().begin(),services.bindings.entries().end(),
                    [&](const auto& binding) { return binding.second==Input::Action::SoftDrop && services.gameplay_key_down(binding.first); }));
        const bool newActionPress=event->down && !Input::is_horizontal(services.bindings.action_for(event->key));
        if (!(instantFall && newActionPress && !config.autoplay) && step(time)) return result;
        services.consume_gameplay_event();
        if (step(time)) return result;
        // A command leaves later events queued for the next piece/Hold.
    }
    step(now);
    return result;
}

} // namespace Engine
