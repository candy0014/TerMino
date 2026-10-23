#ifndef TETR_ENGINE_MULTIPLAYER_SCENE_H
#define TETR_ENGINE_MULTIPLAYER_SCENE_H
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/player_session_state.h"
#include <string>
#include <string_view>
namespace Runtime {class GameContext;}
namespace Engine {
struct GameBoardAnchor {int column,row;};
GameBoardAnchor multiplayer_left_anchor(Runtime::GameContext& context,const Runtime::PlayerSessionState& player,Render::TerminalSize size);
void initialize_multiplayer_scene(Runtime::GameContext& context);
void update_multiplayer_scene(Runtime::GameContext& context);
void poll_multiplayer_scene(Runtime::GameContext& context,bool receiveAttacks=true);
void draw_multiplayer_scene(Runtime::GameContext& context,bool force=false,std::optional<Render::TerminalSize> sizeOverride={});
void position_multiplayer_scene(Runtime::GameContext& context,Render::TerminalSize size);
void cycle_multiplayer_focus(Runtime::GameContext& context);
void finish_multiplayer_scene(Runtime::GameContext& context,bool aborted=false);
std::vector<int> full_multiplayer_slots(Runtime::GameContext& context,std::optional<Render::TerminalSize> sizeOverride={});
std::string compact_player_name(std::string_view name,int columns);
}
#endif
