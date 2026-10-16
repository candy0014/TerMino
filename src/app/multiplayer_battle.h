#ifndef TETR_APP_MULTIPLAYER_BATTLE_H
#define TETR_APP_MULTIPLAYER_BATTLE_H
#include "tetr/render/terminal_renderer.h"
#include <cstddef>
namespace Runtime {class GameContext;}
namespace Engine {class MovementInputState;enum class CountdownResult;}
namespace App {
struct TerminalCanvas;
void paint_room_waiting(Runtime::GameContext& context,TerminalCanvas& surface,
    Render::TerminalSize size,std::size_t choice,std::size_t& offset,bool configuring=false);
bool wait_for_room_ready(Runtime::GameContext& context);
Engine::CountdownResult wait_for_room_round(Runtime::GameContext& context,Engine::MovementInputState& movement);
void watch_room_match(Runtime::GameContext& context,bool localLost=false);
}
#endif
