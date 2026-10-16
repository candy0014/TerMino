#include "tetr/engine/game_end_presentation.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include <iomanip>
#include <iostream>
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/util/numeric.h"
#include "tetr/util/terminal_text.h"

namespace Engine {

void render_final_mode_result(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::Duration elapsedGameTime) {
    Render::OutputGuard output(context);
    const auto goal=Runtime::active_goal(context.config);
    if(goal==Runtime::GameGoal::None) return;
    if(goal!=Runtime::GameGoal::Time){
        const auto elapsed=Util::format_time(elapsedGameTime.seconds());
        if(context.config.display.obscuredColors) Render::set_ui_color(Render::ColorRole::Ghost,true);
        else Render::set_cell_color(3,false);
        Render::move_to(context,player,1,context.config.field.width/2,-((context.config.field.width%2==0)+int(Util::TerminalText::width(elapsed)))/2);
        std::cout<<elapsed;
        fflush(stdout);
    }
    if(goal==Runtime::GameGoal::Time){
        if(context.config.display.obscuredColors) Render::set_ui_color(Render::ColorRole::Ghost,true);
        else Render::set_cell_color(3,false);
        Render::move_to(context,player,1,context.config.field.width/2,-((context.config.field.width%2==0)+Util::digit_count(player.gameplay.scoring.score))/2);
        std::cout<<player.gameplay.scoring.score;
        fflush(stdout);
    }
}

} // namespace Engine
