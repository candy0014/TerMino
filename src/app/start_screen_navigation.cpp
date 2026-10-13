#include "tetr/app/start_screen_navigation.h"
#include "tetr/app/application_state.h"
#include "tetr/input/services.h"
#include "tetr/engine/pause.h"
#include "tetr/runtime/game_context.h"

namespace App {
void request_start_screen(Runtime::GameContext& context) {
    if (auto* app = context.app_component()) app->startScreenRequested = true;
    throw StartScreenRequested{};
}

void install_start_screen_navigation(Runtime::GameContext& context) {
    context.input_services().discard_control_events();
    const auto navigation = [&context] {
#ifdef _WIN32
        constexpr int escape = 0x1b;
#else
        constexpr int escape = 1;
#endif
        const auto* app = context.app_component();
        bool escapeBound=context.input_services().bindings.action_for(escape)!=Input::Action::Unknown;
        if (app && context.config.mode.mode==Runtime::GameMode::LocalVersus) {
            for (const auto* player:{app->primaryContext,app->localPlayerTwoContext})
                if (player && player->input_services().bindings.action_for(escape)!=Input::Action::Unknown)
                    escapeBound=true;
        }
        if ((app && app->startScreenRequested) ||
            (!escapeBound && context.input_services().control_key_pressed(escape)))
            request_start_screen(context);
    };
    context.input_services().controlInterrupt = [&context, navigation] {
        navigation();
        Engine::poll_game_pause(context, navigation);
    };
    context.services.clock.waitInterrupt = [&context] {
        context.input_services().poll_control_interrupt();
    };
}
}
