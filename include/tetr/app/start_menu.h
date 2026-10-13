#ifndef TETR_APP_START_MENU_H
#define TETR_APP_START_MENU_H
namespace Runtime { class GameContext; }
namespace App {
inline constexpr const char* kGameName = "TerMino";
void draw_start_menu(const Runtime::GameContext& context);
// False means Quit. No game clock or pieces are initialized until Play.
bool run_start_menu(Runtime::GameContext& context);
}
#endif
