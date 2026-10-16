#include "tetr/engine/multiplayer_scene.h"
#include "tetr/runtime/game_context.h"
#include "tetr/render/board_renderer.h"
#include <cmath>
#include <algorithm>

namespace Engine {
GameBoardAnchor multiplayer_left_anchor(Runtime::GameContext& context,const Runtime::PlayerSessionState& player,Render::TerminalSize size) {
    const auto& scene=context.session.roomScene;
    if(!scene.spectator && scene.selfAlive && (scene.survivors.size()==2 || (scene.survivors.size()<2 && scene.finalists.size()==2)) && scene.finalBattleAnchor) {
        const auto& anchor=*scene.finalBattleAnchor;
        if(anchor.columns==size.columns && anchor.rows==size.rows && anchor.width==player.gameplay.playfield.width && anchor.height==player.gameplay.playfield.visibleHeight)
            return {anchor.column,anchor.row};
    }
    const auto bounds=Render::board_horizontal_extent(context,player,false);
    const auto core=Render::board_core_horizontal_extent(context,player,false);
    const auto vertical=Render::board_vertical_extent(context,player,false);
    const int left=bounds.left-player.presentation.margin,right=bounds.right-player.presentation.margin;
    const int span=right-left,optional=core.left-bounds.left,offset=-left;
    const int copies=span*2+6<=size.columns?2:1,total=span*copies+4*(copies-1);
    double x=0,y=0,weight=0;
    const auto mass=[&](double a,double b,double width,double height,double density) {
        const double value=width*height*density;x+=(a+width/2)*value;y+=(b+height/2)*value;weight+=value;
    };
    const int width=player.gameplay.playfield.width*2,height=player.gameplay.playfield.visibleHeight;
    mass(offset,0,width,height,2);
    mass(offset+left+optional,0,-left-optional,player.presentation.hold_panel_height()+1,.5);
    mass(offset+left+optional,Render::board_info_top_row(player),-left-optional,15,.5);
    if(player.gameplay.playfield.nextCount>0)mass(offset+width+1,0,right-width-1,
        player.presentation.next_panel_height(player.gameplay.playfield.nextCount),1);
    const double centerX=weight?x/weight+(copies==2?(span+4)/2.:0):0,centerY=weight?y/weight:0;
    const int ideal=static_cast<int>(std::lround(size.columns/2.-centerX));
    int dx;
    if(size.columns>=total+2)dx=std::clamp(ideal,1,size.columns-total-1);
    else if(size.columns>=total-optional+2)dx=std::clamp(ideal,1-optional,size.columns-total-1);
    else dx=std::clamp(ideal,std::min(-optional,size.columns-total),-optional);
    int dy=size.rows-vertical.coreBottom-1;
    if(dy>=Runtime::kDisplayHiddenRows) {
        const int high=std::max(Runtime::kDisplayHiddenRows,std::min(dy,size.rows-vertical.fullBottom-1));
        dy=std::clamp(static_cast<int>(std::lround(size.rows/2.-centerY)),Runtime::kDisplayHiddenRows,high);
    }
    return {dx+offset,dy};
}

// Countdown and gameplay share this presentation-only selection. Keeping it
// here avoids a dependency from presentation back into network integration.
std::vector<int> full_multiplayer_slots(Runtime::GameContext& context,std::optional<Render::TerminalSize> sizeOverride) {
    const auto viewport=sizeOverride.value_or(Render::terminal_size().value_or(Render::TerminalSize{120,40}));
    const auto& scene=context.session.roomScene;
    if(scene.survivors.size()==2 || (scene.survivors.size()<2 && scene.finalists.size()==2)) {
        auto slots=scene.finalistLayout.size()==2?scene.finalistLayout:scene.survivors.size()==2?scene.survivors:scene.finalists;
        const int main=(!scene.spectator && scene.selfAlive)?scene.ownSlot:scene.focusSlot;
        if(scene.finalistLayout.size()!=2 && slots.back()==main) std::swap(slots.front(),slots.back());
        const auto player=[&](int slot)->const Runtime::PlayerSessionState* {
            if(slot==scene.ownSlot)return &context.session.player;
            const auto found=scene.players.find(slot);return found==scene.players.end()?nullptr:&found->second;
        };
        auto* first=player(slots.front());auto* second=player(slots.back());
        if(first && second) {
            const auto size=viewport;
            const auto anchor=multiplayer_left_anchor(context,*first,size);
            const auto a=Render::board_horizontal_extent(context,*first,false),b=Render::board_horizontal_extent(context,*second,false);
            const int right=anchor.column+(a.right-first->presentation.margin)+4+(b.right-b.left);
            if(right>=size.columns)return {slots.front()};
        }
        return slots;
    }
    const int slot=(!scene.spectator && scene.selfAlive)?scene.ownSlot:scene.focusSlot;
    return slot<0?std::vector<int>{}:std::vector<int>{slot};
}

} // namespace Engine
