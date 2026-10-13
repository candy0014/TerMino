#ifndef TETR_APP_SETTINGS_MENU_H
#define TETR_APP_SETTINGS_MENU_H

namespace Runtime { class GameContext; }

namespace App::Settings {
enum class MenuResult { StartGame, StartScreen };
MenuResult run_menu(Runtime::GameContext& context, bool fromStart = false);
// Recover a missing/corrupt map selection in the affected player's Maps menu.
MenuResult run_network_menu(Runtime::GameContext& context);
MenuResult run_map_menu(Runtime::GameContext& context);
// Appearance fields are shared; gameplay settings and profile paths remain
// per-player. The displayed profile supplies the shared values.
bool synchronize_shared_appearance(Runtime::GameContext& displayedContext, bool save = false);
void initialize_local_versus_player_settings(Runtime::GameContext& context);
void adopt_single_player_settings(Runtime::GameContext& displayedContext);
}

#endif
