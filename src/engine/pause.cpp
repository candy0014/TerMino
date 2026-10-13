#include "tetr/engine/pause.h"
#include "tetr/engine/input_precharge.h"
#include "tetr/engine/undo.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include <iostream>

namespace Engine {
namespace {
bool action_down(Runtime::GameContext& context, Input::Action action, bool queued = false) {
    bool down = false;
    for (const auto& [key, bound] : context.input_services().bindings.entries())
        if (bound == action) down |= queued ? context.input_services().control_key_pressed(key)
                                          : context.input_services().key_down(key);
    return down;
}
void pause_caption(Runtime::GameContext& context, bool visible) {
    Render::OutputGuard output(context);
    Render::set_ui_color(context.config.display.obscuredColors ? Render::ColorRole::Ghost
        : Render::ColorRole::Orange, context.config.display.obscuredColors);
    Render::move_cursor(context.session.player.gameplay.playfield.visibleHeight + context.session.player.presentation.topMargin + 4,
        context.session.player.presentation.margin);
    std::cout << (visible ? "PAUSED" : "      ");
    Render::flush();
}
void resume(Runtime::GameContext& context) {
    const auto ended=context.services.clock.now();
    context.pause.intervals.push_back({context.pause.started,ended,context.pause.removedTime});
    context.pause.removedTime += ended - context.pause.started;
    context.pause.paused = false;
}
}
bool pause_enabled(const Runtime::GameContext& context) noexcept {
    return !context.config.disablePause && !context.config.mode.replayMode &&
        context.config.mode.mode != Runtime::GameMode::Multiplayer &&
        context.config.mode.mode != Runtime::GameMode::LocalVersus &&
        context.config.garbage.mode != Runtime::GarbageMode::Multiplayer;
}
GamePauseScope::GamePauseScope(Runtime::GameContext& context, MovementInputState* input)
    : context_(context), input_(input) {
    context.pause.active = pause_enabled(context);
    if (input) context.pause.prechargeMovement = [this] { precharge(); };
    if (context.input_component()) context.pause.keyDown = action_down(context, Input::Action::Pause, true);
}
GamePauseScope::~GamePauseScope() { end(); }
void GamePauseScope::end() {
    context_.pause.active = false;
    context_.pause.prechargeMovement = {};
    if (context_.pause.paused) resume(context_);
    charging_ = false;
}
void GamePauseScope::precharge() {
    if (!context_.pause.paused || !context_.config.handling.dasPrecharge) { charging_ = false; return; }
    if (!charging_) {
        pausedInput_ = *input_;
        pausedInput_.rebase_timers(context_.pause.started-context_.real_now());
        charging_ = true;
    }
    const auto wallNow=context_.services.clock.now();
    precharge_paused_movement_input(context_,pausedInput_,wallNow);
    // Preserve physical horizontal holds across paused undo, while all other
    // input and piece state still comes from the restored snapshot.
    input_->copy_horizontal_precharge(context_.input_services().bindings,
        pausedInput_,context_.real_now()-wallNow);
}
void poll_game_pause(Runtime::GameContext& context, const std::function<void()>& pollNavigation) {
    auto& pause = context.pause;
    if (!pause.active || pause.polling || !pause_enabled(context) || !context.input_component()) return;
    struct Guard {
        bool& flag;
        explicit Guard(bool& value) : flag(value) { flag = true; }
        ~Guard() { flag = false; }
    } guard(pause.polling);
    const auto toggle = [&] {
        const bool down = action_down(context, Input::Action::Pause, true);
        const bool pressed = down && !pause.keyDown;
        pause.keyDown = down;
        if (!pressed) return;
        if (pause.paused) {
            if (pause.prechargeMovement) pause.prechargeMovement();
            resume(context);
            if (pause.prechargeMovement) pause.prechargeMovement();
            // Ignore gameplay presses accumulated during the pause.
            if (context.input_services().gameplay_input_active()) context.input_services().begin_gameplay_input();
        } else {
            // Reserve before pausing so scope cleanup can resume without allocating.
            if (pause.intervals.size()==pause.intervals.capacity())
                pause.intervals.reserve(std::max<std::size_t>(16,pause.intervals.capacity()*2));
            pause.started = context.services.clock.now();
            pause.paused = true;
            if (context.replay_component()) ++context.replay_component()->pauseCount;
        }
    };
    toggle();
    if (!pause.paused) return;
    pause_caption(context, true);
    while (pause.paused) {
        if (refresh_game_terminal_layout(context)) pause_caption(context,true);
        pollNavigation();
        if (pause.prechargeMovement) pause.prechargeMovement();
        poll_undo_key(context);
        // Let the normal game loop route these actions to restart/settings.
        if (action_down(context, Input::Action::Retry) || action_down(context, Input::Action::OpenSettings)) return;
        toggle();
        if (pause.paused) context.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));
    }
    pause_caption(context, false);
}
}
