#ifndef TETR_APP_START_MENU_H
#define TETR_APP_START_MENU_H
#include "tetr/render/terminal_renderer.h"
#include <optional>
namespace Runtime { class GameContext; }
namespace App {
inline constexpr const char* kGameName = "TerMino";
void draw_start_menu(const Runtime::GameContext& context,
                     std::optional<Render::TerminalSize> size={});
// False means Quit. No game clock or pieces are initialized until Play.
bool run_start_menu(Runtime::GameContext& context);
}
#endif
