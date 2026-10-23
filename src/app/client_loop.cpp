#include "tetr/app/client_loop.h"
#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/app/settings_menu.h"
#include "tetr/app/start_menu.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/network/network_services.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/runtime/game_context.h"
#include "tetr/frontend/ui_surface.h"
#include "multiplayer_battle.h"
#include "ui_text.h"
#include <cstdlib>
namespace App {
int run_client(Runtime::GameContext& context,bool skipStart) {
    auto& applicationState=*context.app_component();
    auto& networkServices=*context.network_component();
    if (!skipStart && !std::getenv("TETR_TEST_BATCH") && !std::getenv("TETR_EXIT_AFTER_GAME") &&
        !App::run_start_menu(context)) {
        Network::disconnect_online(networkServices);
        return 0;
    }
    bool watchInterruptedRoom=false;
    for (;;) {
        if (applicationState.startScreenRequested) {
            watchInterruptedRoom=false;
            applicationState.startScreenRequested = false;
            applicationState.replayResume = {};
            if(networkServices.connection==Network::ConnectionState::Connected && networkServices.multiplayerRoom && !networkServices.room.name.empty())
                Network::send_lobby_signal(context,Network::ClientLobbySignal::Cancel);
            else Network::disconnect_online(networkServices);
            if (!App::run_start_menu(context)) {
                Network::disconnect_online(networkServices);
                return 0;
            }
        }
        try {
            if(watchInterruptedRoom) {
                watchInterruptedRoom=false;
                App::watch_room_match(context);
                if(applicationState.startScreenRequested) continue;
            }
            if(applicationState.beforeGame)applicationState.beforeGame();
            App::initialize_game(context);
            if(applicationState.gameReady)applicationState.gameReady();
            App::run(context);
        } catch (const App::StartScreenRequested&) {
            continue;
        } catch (const App::RoomSettingsRequested&) {
            networkServices.matchCanceled=false;
            App::Settings::run_menu(context);
            continue;
        } catch (const Network::LobbyInterrupted&) {
            if(networkServices.multiplayerRoom && context.session.roomScene.enabled &&
               context.session.roomScene.started && networkServices.matchEpoch!=context.session.roomScene.epoch) {
                auto& scene=context.session.roomScene;
                if(!scene.finished)Engine::finish_multiplayer_scene(context,true);
            }
            if(networkServices.multiplayerRoom && networkServices.matchCanceled) {
                networkServices.matchCanceled=false;
                App::Settings::run_menu(context);
                continue;
            }
            if(networkServices.multiplayerRoom && context.session.roomScene.enabled && !networkServices.startFromMenu &&
                context.session.roomScene.finished && networkServices.matchEpoch!=context.session.roomScene.epoch)
                watchInterruptedRoom=true;
            continue;
        } catch (const Network::ConnectionError& error) {
            if(Frontend::ui_surface_active()) {
                // A failed Start action must explain the failure, rather than
                // silently turning into an Open Settings action.
                applicationState.startMenuNotice=UiText::tr(error.what());
                applicationState.startScreenRequested=true;
                continue;
            }
            applicationState.settingsCategoryIndex = 6; // NETWORK, after Player
            App::Settings::run_menu(context);
            continue;
        } catch (const App::MapUnavailable& unavailable) {
            App::Settings::run_map_menu(unavailable.context);
            continue;
        }
        if (std::getenv("TETR_EXIT_AFTER_GAME")) {
            Network::disconnect_online(networkServices);
            return 0;
        }
    }
}
}
