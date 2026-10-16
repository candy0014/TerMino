#include "tetr/app/start_screen_navigation.h"
#include "tetr/app/application_state.h"
#include "tetr/input/services.h"
#include "tetr/engine/pause.h"
#include "tetr/runtime/game_context.h"
#include "tetr/network/network_services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/round_statistics.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/config/normalization.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/util/terminal_text.h"
#include "ui_text.h"
#include "single_player_challenges.h"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <linux/input.h>
#endif

namespace App {
void request_start_screen(Runtime::GameContext& context) {
    if(auto* app=context.app_component())app->replayPreview.reset();
    restore_challenge_replay(context);
    if(auto* n=context.network_component();n && n->multiplayerRoom && n->initialized) {
        if(context.session.roomScene.enabled && context.session.roomScene.selfAlive && !context.session.roomScene.spectator &&
           context.session.roomScene.epoch==n->matchEpoch && n->matchPhase==Network::RoomMatchPhase::Playing) {
            freeze_post_game_times(context,context.session.player);
            Network::send_round_statistics(context,context.session.player);
        }
        Network::handle_room_game_control(context,false);
    }
    if (auto* app = context.app_component()) app->startScreenRequested = true;
    throw StartScreenRequested{};
}

void install_start_screen_navigation(Runtime::GameContext& context) {
    context.input_services().discard_control_events();
    const auto navigation = [&context] {
        if(auto* network=context.network_component();network && network->initialized) {
            Network::poll_connection(*network);
            if(context.user_settings_component()) Network::synchronize_room_player_mode(*network,
                Runtime::selected_game_mode(context.user_settings())==Runtime::GameMode::Multiplayer,context.user_settings().gameMode);
#ifdef _WIN32
            constexpr int leftControl=VK_LCONTROL,rightControl=VK_RCONTROL,leftShift=VK_LSHIFT,rightShift=VK_RSHIFT,stopKey=VK_F12;
#else
            constexpr int leftControl=KEY_LEFTCTRL,rightControl=KEY_RIGHTCTRL,leftShift=KEY_LEFTSHIFT,rightShift=KEY_RIGHTSHIFT,stopKey=KEY_F12;
#endif
            auto& keys=context.input_services();
            // Observe release even while the modifiers are up, so the next
            // round can use the shortcut again with state-only backends too.
            const bool stopPressed=keys.control_key_pressed(stopKey);
            if(network->multiplayerRoom && context.session.roomScene.enabled && Network::is_room_host(*network) &&
               (network->matchPhase==Network::RoomMatchPhase::Playing || network->matchPhase==Network::RoomMatchPhase::Countdown) &&
               (keys.raw_key_down(leftControl) || keys.raw_key_down(rightControl)) &&
               (keys.raw_key_down(leftShift) || keys.raw_key_down(rightShift)) && stopPressed) {
                keys.suppress_control_key_until_release(stopKey);
                Network::request_room(*network,Network::RoomOperation::EndMatch);
                network->startFromMenu=true;
                throw Network::LobbyInterrupted("Host ended the match");
            }
            const auto now=std::chrono::steady_clock::now();
            if(network->multiplayerRoom && (!context.session.roomScene.enabled || network->matchEpoch!=context.session.roomScene.epoch) &&
                now<network->matchNoticeUntil && now-network->lastNoticePaint>=std::chrono::milliseconds(150) && !Render::output_suppressed()) {
                network->lastNoticePaint=now;
                Render::OutputGuard output(context);
                if(const auto size=Render::terminal_size()) {
                    std::cout<<"\0337";Render::move_cursor(size->rows-1,0);
                    Render::set_ui_color(Render::ColorRole::Accent,context.config.display.obscuredColors);
                    std::cout<<Util::TerminalText::clip(UiText::tr("The host has started the match."),size->columns)<<"\0338";
                }
            }
        }
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
