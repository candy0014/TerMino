#include "tetr/engine/board_lifecycle.h"

#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/frame_board_adapter.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"

#include "tetr/rules/quick_play.h"
#include "tetr/runtime/dynamic_rules.h"

namespace Engine {

void initialize_quick_play(Runtime::GameContext& context, Runtime::PlayerSessionState& player){
    auto& qp=context.session.quickPlay;
    auto& cfg=context.config;
    auto& playfield=player.gameplay.playfield;
    const bool snowball=Runtime::enabled(cfg.quickPlay.snowball);
    qp.garbageMultiple=1;
    cfg.garbage.attackMultiplier=1;
    if (cfg.quickPlay.volatileMode == Runtime::ModifierMode::Enabled) cfg.garbage.attackMultiplier = 2;
    if (Runtime::reverse(cfg.quickPlay.volatileMode)) cfg.garbage.attackMultiplier = 3;
    qp.garbageInTime=Runtime::Duration::from_seconds(1.0);
    if(cfg.quickPlay.allSpin == Runtime::AllSpinMode::Reverse)
    {
        qp.garbageMultiple*=0.5;
        if(cfg.garbage.mode == Runtime::GarbageMode::QuickPlay && !snowball && !cfg.initialMap){
            if(!Runtime::reverse(cfg.quickPlay.messy) && !Runtime::reverse(cfg.quickPlay.doubleHole))
                for(int i=0;i<10;i++) GarbageService::add_buffer(context,player,playfield.field,1,true,
                    static_cast<int>(cfg.garbage.attackMultiplier));
        }
    }
    if (!snowball && !cfg.initialMap && Runtime::reverse(cfg.quickPlay.invisible) && cfg.garbage.mode == Runtime::GarbageMode::QuickPlay &&
        !Runtime::reverse(cfg.quickPlay.messy) && !Runtime::reverse(cfg.quickPlay.doubleHole))
        GarbageService::add_buffer(context,player,playfield.field,3,true,static_cast<int>(3*cfg.garbage.attackMultiplier));
    if(Runtime::reverse(cfg.quickPlay.messy))
    {
        cfg.garbage.singleMessiness=1;
        if (!snowball && !cfg.initialMap) {
            for(int i:{3,7,11})for(int j:{2,7})
            {
                for(int k=-1;k<=1;k++)for(int l=-1;l<=1;l++)if(k||l){
                    if(playfield.activeHeight-i+k<-playfield.visibleHeight) continue;
                    if(j+l<0||j+l>=playfield.width) continue;
                    playfield.field.set_cell(playfield.activeHeight-i+k,j+l,Runtime::BoardCell::garbage());
                    Render::draw_board_cell(context,player,playfield.activeHeight-i+k,j+l,Runtime::BoardCell::garbage());
                }
            }
            playfield.garbageHeight=12;
        }
    }
    if(!snowball && !cfg.initialMap && Runtime::reverse(cfg.quickPlay.doubleHole)){
        for(int i=playfield.activeHeight-12;i<playfield.activeHeight;i++){
            for(int j=0;j<playfield.width;j++) if((i+j)&1){
                playfield.field.set_cell(i,j,Runtime::BoardCell::garbage());
                Render::draw_board_cell(context,player,i,j,Runtime::BoardCell::garbage());
            }
        }
        playfield.garbageHeight=12;
    }
    if (cfg.garbage.mode == Runtime::GarbageMode::QuickPlay) {
        cfg.garbage.incomingDelay = Rules::qp_garbage_delay(cfg.quickPlay,qp.floor,0);
        Runtime::set_current_incoming_garbage_delay(context,cfg.garbage.incomingDelay);
        cfg.garbage.singleMessiness = Rules::qp_messiness(cfg.quickPlay,qp.floor,qp.progress,0);
        auto state = capture_frame_rule_state(player,context);
        state.survivalApm = Rules::qp_survival_apm(qp.altitude);
        if (cfg.quickPlay.gravity == Runtime::ModifierMode::Enabled &&
            !state.gravitySpeed.is_instant() && !state.gravitySpeed.is_disabled()) {
            const double period = state.gravitySpeed.acceleration_seed_period().seconds();
            state.gravitySpeed = Runtime::GravityInterval::periodic(
                Runtime::Duration::from_seconds(1 / (1 / period + 18 * (qp.floor - 1))));
        }
        Rules::advance_frame_after_presentation(state,make_frame_rule_config(context,{}),{});
        store_frame_rule_state(player,state,context);
        // One reception multiplier; EXPERT changes difficulty through messiness,
        // favor, booster rewards and delay rather than doubling attack.
        qp.garbageMultiple = 1;
    }
}

} // namespace Engine
