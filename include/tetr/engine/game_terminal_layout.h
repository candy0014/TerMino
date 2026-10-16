#ifndef TETR_ENGINE_GAME_TERMINAL_LAYOUT_H
#define TETR_ENGINE_GAME_TERMINAL_LAYOUT_H
#include "tetr/render/terminal_renderer.h"
namespace Runtime { class GameContext; struct PlayerSessionState; }
namespace Engine {
struct GameTerminalRequirements { Render::TerminalSize minimum, full; };
GameTerminalRequirements game_terminal_requirements(Runtime::GameContext& context,
    Runtime::GameContext* secondContext=nullptr);
Render::TerminalSize required_game_terminal_size(Runtime::GameContext& context,
    Runtime::GameContext* secondContext=nullptr, bool compact=false);
// Before drawing / launching local workers, positions every board together.
void center_game_boards(Runtime::GameContext& context, Render::TerminalSize size,
    Runtime::GameContext* secondContext=nullptr);
// During play each worker repaints only its own board, under the shared output
// lock. A resize clears once and gives both workers a new layout revision.
bool refresh_game_terminal_layout(Runtime::GameContext& context);
// Move only the peer toward the primary information area, clamping its
// visible footprint before sacrificing any extra rows to alignment.
int aligned_opponent_top(const Runtime::PlayerSessionState& primary,
    const Runtime::PlayerSessionState& peer,int primaryTop,int terminalRows);
}
#endif
