#ifndef TETR_APP_GAME_TERMINAL_H
#define TETR_APP_GAME_TERMINAL_H

#include "tetr/render/terminal_renderer.h"
#include <functional>

namespace Runtime { class GameContext; }
namespace App::InitDetail {
enum class TerminalCheckResult { Ready, OpenSettings };
using TerminalSizeQuery=std::function<std::optional<Render::TerminalSize>()>;

Render::TerminalSize required_game_terminal_size(Runtime::GameContext& context,
    Runtime::GameContext* secondContext=nullptr, bool compact=false);
TerminalCheckResult wait_for_game_terminal(Runtime::GameContext& context,
    Runtime::GameContext* secondContext=nullptr, TerminalSizeQuery query={});
} // namespace App::InitDetail
#endif
