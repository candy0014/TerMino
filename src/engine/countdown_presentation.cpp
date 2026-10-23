#include "tetr/input/services.h"
#include "tetr/engine/countdown_presentation.h"
#include "tetr/engine/input_precharge.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/input/action.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/rules/game_result.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/util/terminal_text.h"

#include <array>
#include <cmath>
#include <set>
#include <string>

#include <iostream>

namespace Engine {

static void draw_countdown_shape(Runtime::GameContext& context,Runtime::PlayerSessionState& player,int verticalOffset,const std::array<const char*,5>& rows) {
    Render::OutputGuard output(context);
    for(int i=0;i<5;i++){
        const auto& board=player.gameplay.playfield;
        const int row=i+1+verticalOffset,offset=-(board.width%2==0)-2;
        Render::move_to(context,player,row,board.width/2,offset);std::cout<<"     ";
        if(board.field.initialized() && row>=-Runtime::kDisplayHiddenRows && row<board.field.height())
            for(int col=0;col<board.width;++col) Render::draw_board_cell(context,player,row,col,board.field.cell(row,col));
        Render::set_cell_color(3,context.config.display.obscuredColors);
        // Spaces in a digit are transparent to the restored field. Only its
        // strokes cover cells; the final blank restores the whole footprint.
        for(int column=0;column<5;++column) {
            const auto glyph=Util::TerminalText::slice(rows[i],column,1);
            if(glyph==" ") continue;
            Render::move_to(context,player,row,board.width/2,offset+column);std::cout<<glyph;
        }
    }
}

struct CountdownInputState {
    std::set<int> suppressed;
};

static void suppress_held_countdown_keys(
    Runtime::GameContext& context,
    CountdownInputState& state) {
    for(const auto& [key, action]:context.input_services().bindings.entries()){
        if((action==Input::Action::Retry || action==Input::Action::OpenSettings) &&
            context.input_services().key_down(key)) state.suppressed.insert(key);
    }
}

static CountdownResult poll_countdown_input(
    Runtime::GameContext& context,
    CountdownInputState& state,
    const std::function<bool(CountdownResult)>& allowControl) {
    for(const auto& [key, action]:context.input_services().bindings.entries()){
        if(action!=Input::Action::Retry && action!=Input::Action::OpenSettings) continue;
        const bool down=context.input_services().key_down(key);
        if(!down) state.suppressed.erase(key);
        else if(!state.suppressed.count(key)) {
            const auto result=action==Input::Action::Retry ? CountdownResult::Retry : CountdownResult::OpenSettings;
            if(!allowControl || allowControl(result)) return result;
        }
    }
    return CountdownResult::Completed;
}

static CountdownResult wait_countdown_second(
    Runtime::GameContext& context,
    Runtime::GameContext* secondContext,
    Runtime::TimePoint start,
    CountdownInputState& firstInput,
    CountdownInputState& secondInput,
    bool onlineMultiplayer,
    MovementInputState* firstMovementInput,
    MovementInputState* secondMovementInput,
    const std::function<bool()>& refreshLayout,
    const std::function<void()>& redraw,
    const std::function<bool(CountdownResult)>& allowControl) {
    const Runtime::Duration countdown = Runtime::Duration::from_seconds(1.0);
    const Runtime::Duration pollInterval = Runtime::Duration::from_seconds(0.001);
    while(context.real_now()-start<countdown){
        if (refreshLayout && refreshLayout()) redraw();
        if (firstMovementInput) precharge_movement_input(context,*firstMovementInput);
        if (secondContext && secondMovementInput) precharge_movement_input(*secondContext,*secondMovementInput);
        (void)onlineMultiplayer;
        const auto firstResult=poll_countdown_input(context,firstInput,allowControl);
        if(firstResult!=CountdownResult::Completed) return firstResult;
        if(secondContext){
            const auto secondResult=poll_countdown_input(*secondContext,secondInput,{});
            if(secondResult!=CountdownResult::Completed) return secondResult;
        }
        context.services.clock.sleep_for(pollInterval);
    }
    return CountdownResult::Completed;
}

static CountdownResult run_countdown(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& first,
    Runtime::GameContext* secondContext,
    Runtime::PlayerSessionState& second,
    bool drawSecond,
    bool onlineMultiplayer,
    MovementInputState* firstMovementInput,
    MovementInputState* secondMovementInput,
    const std::function<bool()>& refreshLayout,
    const std::function<bool(CountdownResult)>& allowControl,
    const std::function<Runtime::Duration()>& remaining = {}) {
    struct CountdownPresentation {
        Runtime::SessionState& first;
        Runtime::GameContext* second;
        void show(int digit) {
            first.countdownDigit=digit;
            if(second)second->session.countdownDigit=digit;
        }
        ~CountdownPresentation() {show(0);}
    } presentation{context.session,secondContext};
    presentation.show(0);
    if (firstMovementInput) precharge_movement_input(context,*firstMovementInput);
    if (secondContext && secondMovementInput) precharge_movement_input(*secondContext,*secondMovementInput);
    if(Runtime::active_goal(context.config)==Runtime::GameGoal::None && context.config.garbage.mode != Runtime::GarbageMode::Multiplayer) return CountdownResult::Completed;
    const int db1=std::min(0,first.gameplay.playfield.activeHeight-first.gameplay.playfield.garbageHeight-6);
    const int db2=std::min(0,second.gameplay.playfield.activeHeight-second.gameplay.playfield.garbageHeight-6);

    static const std::array<std::array<const char*,5>,3> shapes={{
        {{"┏━━━┓","    ┃","  ━━┫","    ┃","┗━━━┛"}},
        {{"┏━━━┓","    ┃","┏━━━┛","┃    ","┗━━━┛"}},
        {{"  ┓  ","  ┃  ","  ┃  ","  ┃  ","  ┻  "}}
    }};
    static const std::array<const char*,5> blank={{"     ","     ","     ","     ","     "}};

    CountdownInputState firstInput;
    CountdownInputState secondInput;
    suppress_held_countdown_keys(context,firstInput);
    if(secondContext) suppress_held_countdown_keys(*secondContext,secondInput);

    const auto draw=[&](const auto& shape) {
        if(context.session.roomScene.enabled) {
            for(int slot:full_multiplayer_slots(context)) {
                if(slot!=context.session.roomScene.ownSlot && !context.session.roomScene.players.count(slot)) continue;
                auto& player=slot==context.session.roomScene.ownSlot ? first : context.session.roomScene.players.at(slot);
                const int offset=std::min(0,player.gameplay.playfield.activeHeight-player.gameplay.playfield.garbageHeight-6);
                draw_countdown_shape(context,player,offset,shape);
            }
        } else {
            draw_countdown_shape(context,first,db1,shape);
            if(drawSecond) draw_countdown_shape(secondContext?*secondContext:context,second,db2,shape);
        }
    };

    if(remaining) {
        int shown=-1;
        for(;;) {
            const bool resized=refreshLayout && refreshLayout();
            const double seconds=remaining().seconds();
            if(seconds<=0) break;
            const int digit=std::clamp(static_cast<int>(std::ceil(seconds)),1,3);
            presentation.show(digit);
            if(digit!=shown || resized) {draw(shapes[3-digit]);Render::flush();shown=digit;}
            if(firstMovementInput) precharge_movement_input(context,*firstMovementInput);
            const auto result=poll_countdown_input(context,firstInput,allowControl);
            if(result!=CountdownResult::Completed) return result;
            context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
        }
    } else for(int digit=3;digit>=1;--digit){
        const auto& shape=shapes[3-digit];
        const Runtime::TimePoint started=context.real_now();
        presentation.show(digit);
        draw(shape);
        fflush(stdout);
        const auto result=wait_countdown_second(
            context,secondContext,started,firstInput,secondInput,onlineMultiplayer,
            firstMovementInput,secondMovementInput,refreshLayout,[&] {
                draw(shape);
            },allowControl);
        if(result!=CountdownResult::Completed) return result;
    }
    draw(blank);
    fflush(stdout);
    return CountdownResult::Completed;
}

CountdownResult run_start_countdown(Runtime::GameContext& context,Runtime::PlayerSessionState& first,
    Runtime::PlayerSessionState& second, MovementInputState* movementInput,
    const std::function<bool()>& refreshLayout,
    const std::function<bool(CountdownResult)>& allowControl,
    const std::function<Runtime::Duration()>& remaining) {
    const bool multiplayer=!context.config.mode.replayMode&&
        context.config.garbage.mode==Runtime::GarbageMode::Multiplayer;
    return run_countdown(context,first,nullptr,second,multiplayer && !context.session.roomScene.enabled,multiplayer,movementInput,nullptr,refreshLayout,allowControl,remaining);
}

CountdownResult run_local_versus_countdown(
    Runtime::GameContext& firstContext,
    Runtime::PlayerSessionState& first,
    Runtime::GameContext& secondContext,
    Runtime::PlayerSessionState& second,
    MovementInputState* firstMovementInput,
    MovementInputState* secondMovementInput,
    const std::function<bool()>& refreshLayout) {
    return run_countdown(firstContext,first,&secondContext,second,true,false,firstMovementInput,secondMovementInput,refreshLayout,{});
}

} // namespace Engine
