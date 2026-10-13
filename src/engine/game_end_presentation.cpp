#include "tetr/engine/game_end_presentation.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include <iomanip>
#include <iostream>
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/util/numeric.h"

namespace Engine {

void render_final_mode_result(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::Duration elapsedGameTime) {
    Render::OutputGuard output(context);
    if(context.config.mode.mode != Runtime::GameMode::Racing&&context.config.mode.mode != Runtime::GameMode::Blitz) return;
    if(context.config.mode.mode == Runtime::GameMode::Racing){
        const double elapsed=elapsedGameTime.seconds();
        if(context.config.display.obscuredColors) Render::set_ui_color(Render::ColorRole::Ghost,true);
        else Render::set_cell_color(3,false);
        Render::move_to(context,player,1,context.config.field.width/2,-((context.config.field.width%2==0)+(Util::digit_count((int)elapsed))+4)/2);
        std::cout<<std::fixed<<std::setprecision(3)<<elapsed<<std::defaultfloat;
        fflush(stdout);
    }
    if(context.config.mode.mode == Runtime::GameMode::Blitz){
        if(context.config.display.obscuredColors) Render::set_ui_color(Render::ColorRole::Ghost,true);
        else Render::set_cell_color(3,false);
        Render::move_to(context,player,1,context.config.field.width/2,-((context.config.field.width%2==0)+Util::digit_count(player.gameplay.scoring.score))/2);
        std::cout<<player.gameplay.scoring.score;
        fflush(stdout);
    }
}

} // namespace Engine
