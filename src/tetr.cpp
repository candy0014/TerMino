#include "tetr/app/client_loop.h"
#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/app/settings_menu.h"
#include "tetr/app/start_menu.h"
#include "tetr/app/start_screen_navigation.h"
#include "app/ui_text.h"
#include "app/multiplayer_battle.h"
#include "tetr/network/network_services.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/config/user_config.h"
#include "tetr/config/profile_store.h"
#include "tetr/util/runtime_directory_lock.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

const char* config_path_from_environment(const char* name, const char* fallback) {
    const char* path = std::getenv(name);
    return path && *path != '\0' ? path : fallback;
}

} // namespace

int main(int argc, char* argv[]) {
    const char* playerOneBindings = UserConfig::kDefaultKeybindingPath;
    const char* playerTwoBindings = UserConfig::kDefaultPlayerTwoKeybindingPath;
    bool continuousCancellation = false;
    bool skipStart = false,migrateConfig=false;
    int bindingPaths = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if(argument=="--migrate-config"){migrateConfig=true;}
        else if (argument == "--qp-continuous-cancel") {
            continuousCancellation = true;
        } else if (argument == "--play") {
            skipStart = true;
        } else if (argument == "--help") {
            std::puts("TerMino\nUsage: tetr [--play] [--qp-continuous-cancel] [player1-keybindings] [player2-keybindings]\n"
                      "  --migrate-config        Import existing profiles into .tetr/config.json and exit.\n"
                      "  --play                  Start immediately with the saved configuration.\n"
                      "  --qp-continuous-cancel  Enable Quick Play continuous cancellation penalties (default: off).");
            return 0;
        } else if (argument.substr(0, 2) == "--" || bindingPaths >= 2) {
            std::fprintf(stderr, "Unexpected argument: %s\nUse --help for usage.\n", argv[i]);
            return 2;
        } else if (bindingPaths++ == 0) {
            playerOneBindings = argv[i];
        } else {
            playerTwoBindings = argv[i];
        }
    }
    Util::RuntimeDirectoryLock directoryLock(".tetr/client.lock");
    if(!directoryLock.acquired()) {std::fprintf(stderr,"TerMino: %s\n",directoryLock.error().c_str());return 3;}
    if(migrateConfig){const bool saved=UserConfig::migrate_client_config();std::fputs(saved?"Client settings migrated to .tetr/config.json\n":"Could not migrate client settings; existing files were preserved.\n",saved?stdout:stderr);return saved?0:1;}
    Runtime::SessionState session;
    Runtime::RuntimeServices runtimeServices;
    Runtime::GameContext context(session, runtimeServices);
    Runtime::SessionState playerTwoSession;
    Runtime::RuntimeServices playerTwoRuntimeServices;
    Runtime::GameContext playerTwoContext(playerTwoSession, playerTwoRuntimeServices);
    App::ApplicationState applicationState;
    applicationState.qpContinuousCancellation = continuousCancellation;
    Network::NetworkServices networkServices;
    Network::NetworkServices playerTwoNetworkServices;
    Replay::ReplaySessionState replaySession;
    Replay::ReplaySessionState playerTwoReplaySession;
    Legacy::BotStrategy botStrategy;
    Legacy::BotStrategy playerTwoBotStrategy;
    Input::Services inputServices;
    Input::Services playerTwoInputServices;
    UserConfig::Settings userSettings;
    UserConfig::Settings playerTwoUserSettings;
    context.attach_app(applicationState);
    context.attach_network(networkServices);
    context.attach_replay(replaySession);
    context.attach_autoplay(botStrategy);
    context.attach_input(inputServices);
    context.attach_user_settings(userSettings);
    context.attach_output_mutex(applicationState.terminalOutputMutex);

    playerTwoContext.attach_app(applicationState);
    playerTwoContext.attach_network(playerTwoNetworkServices);
    playerTwoContext.attach_replay(playerTwoReplaySession);
    playerTwoContext.attach_autoplay(playerTwoBotStrategy);
    playerTwoContext.attach_input(playerTwoInputServices);
    playerTwoContext.attach_user_settings(playerTwoUserSettings);
    playerTwoContext.attach_output_mutex(applicationState.terminalOutputMutex);
    applicationState.primaryContext = &context;
    applicationState.localPlayerTwoContext = &playerTwoContext;

    auto& user = userSettings;
    setvbuf(stdout, nullptr, _IOFBF, 4096);
    UserConfig::load_keybindings(user, playerOneBindings);
    try {
    UserConfig::load_startup_settings(user, false, config_path_from_environment("TETR_USER_CONFIG", ""));
    playerTwoUserSettings = userSettings;
    UserConfig::load_player_two_keybindings(
        playerTwoUserSettings,
        playerTwoBindings);
    UserConfig::load_startup_settings(playerTwoUserSettings, true,
        config_path_from_environment("TETR_USER_CONFIG_PLAYER2", ""));
    } catch(const std::exception& error) {std::fprintf(stderr,"TerMino: %s\n",error.what());return 1;}
    applicationState.localVersusSettingsAvailable =
        UserConfig::has_saved_profile(playerTwoUserSettings);
    if (!std::getenv("TETR_USER_CONFIG")) {
        const int owner=UserConfig::load_active_player();
        if (owner == 1) {
            applicationState.singlePlayerOwner = 1;
            applicationState.savedPlayerOneSettings = user;
            user = playerTwoUserSettings;
        }
    }
    Input::refresh_bindings(context.input_services(), context.user_settings().keyCodes, context.user_settings().additionalKeyCodes);
    Input::refresh_bindings(
        playerTwoContext.input_services(),
        playerTwoContext.user_settings().keyCodes, playerTwoContext.user_settings().additionalKeyCodes);
    Input::initialize_services(inputServices);
    Input::share_key_state(playerTwoInputServices, inputServices);
    App::install_start_screen_navigation(context);
    App::install_start_screen_navigation(playerTwoContext);
    App::UiText::initialize_language(user.uiLanguage);
    user.uiLanguage =
        App::UiText::language() == App::UiText::Language::English ? 1 : 0;
    App::Settings::synchronize_shared_appearance(context);
    Render::TerminalSession terminalSession(&inputServices);
    applicationState.textInputSession = &terminalSession;
    if (std::getenv("TETR_TEST_BATCH")) Render::begin_output_suppression();
    return App::run_client(context,skipStart);
}
