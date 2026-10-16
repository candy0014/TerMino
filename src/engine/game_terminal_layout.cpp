#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/network/network_services.h"
#include "tetr/app/application_state.h"
#include "tetr/render/board_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <limits>
#include "tetr/rules/quick_play.h"

namespace Engine {
namespace {
struct Slot { Runtime::GameContext* context; Runtime::PlayerSessionState* player; int offset; };
struct Layout {
    std::vector<Slot> slots;
    int columns=0, coreBottom=0, fullBottom=0, optionalLeft=0;
    double visualX=0, visualY=0, weight=0;
};
void mass(Layout& value,double left,double top,double width,double height,double density) {
    const double weight=width*height*density;
    value.visualX+=(left+width/2)*weight;
    value.visualY+=(top+height/2)*weight;
    value.weight+=weight;
}
Layout layout(Runtime::GameContext& context, Runtime::GameContext* second, bool maximum,
    Runtime::PlayerSessionState* primary=nullptr,Runtime::PlayerSessionState* peer=nullptr,bool single=false) {
    Layout result;
    const auto append=[&](Runtime::GameContext& owner,Runtime::PlayerSessionState& player) {
        const auto bounds=Render::board_horizontal_extent(owner,player,maximum);
        const auto vertical=Render::board_vertical_extent(owner,player,maximum);
        const int left=bounds.left-player.presentation.margin;
        const int right=bounds.right-player.presentation.margin;
        const int offset=result.columns+(result.slots.empty() ? 0 : 4)-left;
        const auto core=Render::board_core_horizontal_extent(owner,player,maximum);
        const int optional=core.left-bounds.left;
        if (result.slots.empty()) result.optionalLeft=optional;
        result.slots.push_back({&owner,&player,offset});
        result.columns=offset+right;
        result.coreBottom=std::max(result.coreBottom,vertical.coreBottom);
        result.fullBottom=std::max(result.fullBottom,vertical.fullBottom);
        const int snowball=maximum && Runtime::enabled(owner.config.quickPlay.snowball)
            ? Rules::qp_snowball_size(std::numeric_limits<int>::max()) : 0;
        const int width=std::max(player.gameplay.playfield.width,snowball)*2;
        const int height=std::max(player.gameplay.playfield.visibleHeight,snowball);
        // Keep the field as the main focus. NEXT's repeated colored pieces
        // carry more visual weight than the sparse HOLD/statistics panel.
        mass(result,offset,0,width,height,2);
        mass(result,offset+left+optional,0,-left-optional,player.presentation.hold_panel_height()+1,0.5);
        mass(result,offset+left+optional,Render::board_info_top_row(player),-left-optional,15,0.5);
        if (player.gameplay.playfield.nextCount>0)
            mass(result,offset+width+1,0,right-width-1,
                player.presentation.next_panel_height(player.gameplay.playfield.nextCount),1);
    };
    append(context,primary?*primary:context.session.player);
    if (second) append(*second,second->session.player);
    else if (!single && !context.config.mode.replayMode && context.config.garbage.mode==Runtime::GarbageMode::Multiplayer)
        append(context,peer?*peer:context.session.opponent);
    for (const auto& slot:result.slots) {
        if (slot.player!=&slot.context->session.player) continue;
        const auto replay=replay_playback_terminal_size(*slot.context,*slot.player,true,maximum);
        if (replay.columns>0) {
            const auto text=Render::board_text_layout(*slot.context,*slot.player);
            const int width=replay.columns-text.column;
            result.columns=std::max(result.columns,slot.offset+replay.columns-slot.player->presentation.margin);
            result.coreBottom=std::max(result.coreBottom,replay.rows);
            result.fullBottom=std::max(result.fullBottom,replay.rows);
            mass(result,slot.offset+text.column-slot.player->presentation.margin,replay.rows-3,width,3,0.5);
        }
    }
    result.visualX/=result.weight;
    result.visualY/=result.weight;
    return result;
}
int horizontal_translation(const Layout& value,int columns) {
    const int ideal=static_cast<int>(std::lround(columns/2.0-value.visualX));
    if (columns>=value.columns+2) return std::clamp(ideal,1,columns-value.columns-1);
    if (columns>=value.columns-value.optionalLeft+2)
        return std::clamp(ideal,1-value.optionalLeft,columns-value.columns-1);
    // If even the whole layout cannot fit, maximize its visible intersection.
    return std::clamp(ideal,std::min(-value.optionalLeft,columns-value.columns),-value.optionalLeft);
}
int vertical_translation(const Layout& value,int rows) {
    int low=0,high=rows-value.coreBottom-1;
    if (high<low) return high; // Bottom information wins over the upper field.
    if (high<Runtime::kDisplayHiddenRows) return high;
    low=Runtime::kDisplayHiddenRows;
    const int withNext=rows-value.fullBottom-1;
    if (withNext<low) return low; // Preserve the eight rows before NEXT.
    high=std::min(high,withNext);
    return std::clamp(static_cast<int>(std::lround(rows/2.0-value.visualY)),low,high);
}
std::array<int,10> signature(const Layout& value,Render::TerminalSize size) {
    return {{size.columns,size.rows,value.columns,value.coreBottom,value.fullBottom,
        value.slots.front().offset,value.slots.back().offset,
        static_cast<int>(std::lround(value.visualX*1024)),
        static_cast<int>(std::lround(value.visualY*1024)),static_cast<int>(value.slots.size())}};
}
void position(Slot slot,int dx,int dy,Render::TerminalSize size,std::uint64_t revision) {
    auto& p=slot.player->presentation;
    if (p.margin!=dx+slot.offset || p.topMargin!=dy || p.terminalLayoutRevision!=revision) {
        p.altitudeDisplay={}; p.undoCountDisplay={}; p.replayPromptDisplay.clear();
        p.windupDisplay={}; p.windupDisplayedSegments=-1;
    }
    p.margin=dx+slot.offset; p.topMargin=dy; p.dynamicLayout=true;
    p.terminalColumns=size.columns; p.terminalRows=size.rows;
    p.layoutBoardWidth=slot.player->gameplay.playfield.width;
    p.layoutBoardHeight=slot.player->gameplay.playfield.visibleHeight;
    p.layoutNextCount=slot.player->gameplay.playfield.nextCount;
    p.terminalLayoutRevision=revision;
}
void position_layout(const Layout& value,int dx,int dy,Render::TerminalSize size,std::uint64_t revision) {
    for(std::size_t index=0;index<value.slots.size();++index) {
        const auto& slot=value.slots[index];
        const int top=index==0?dy:aligned_opponent_top(*value.slots.front().player,*slot.player,dy,size.rows);
        position(slot,dx,top,size,revision);
    }
}
Runtime::GameContext* local_second(Runtime::GameContext& primary) {
    const auto* app=primary.app_component();
    return app && (primary.config.mode.mode==Runtime::GameMode::LocalVersus ||
        (primary.replay_component() && primary.replay_component()->localVersusReplay))
        ? app->localPlayerTwoContext : nullptr;
}
}

int aligned_opponent_top(const Runtime::PlayerSessionState& primary,const Runtime::PlayerSessionState& peer,
    int primaryTop,int terminalRows) {
    const int desired=primaryTop+Render::board_info_top_row(primary)-Render::board_info_top_row(peer);
    const auto& field=peer.gameplay.playfield;const auto& p=peer.presentation;
    const int bottom=std::max({field.visibleHeight+1,Render::board_info_top_row(peer)+14,p.hold_panel_height(),
        field.nextCount>0?p.next_panel_height(field.nextCount):0});
    const int highest=terminalRows-bottom-1;
    if(highest>=0)return std::clamp(desired,0,highest);
    const int fieldHighest=terminalRows-field.visibleHeight-2;
    // Extra-tall NEXT/HUD panels must not hide a field that can fit. When
    // the field itself is too tall, keep its floor and name and crop above.
    return fieldHighest>=0?std::clamp(desired,0,fieldHighest):fieldHighest;
}

GameTerminalRequirements game_terminal_requirements(Runtime::GameContext& context,Runtime::GameContext* second) {
    const bool room=context.session.roomScene.enabled || (context.network_component() && context.network_component()->multiplayerRoom &&
        context.config.mode.mode==Runtime::GameMode::Multiplayer && !context.config.mode.replayMode);
    const auto value=layout(context,second,true,nullptr,nullptr,room);
    const Render::TerminalSize minimum{value.columns-value.optionalLeft+2,value.coreBottom+2};
    const int fullWidth=static_cast<int>(std::ceil(2*std::max(value.visualX+1,value.columns-value.visualX+1)));
    const int fullHeight=static_cast<int>(std::ceil(2*std::max(value.visualY+Runtime::kDisplayHiddenRows,
        value.fullBottom-value.visualY+1)));
    return {minimum,{std::max(minimum.columns,fullWidth),
        std::max({minimum.rows,value.fullBottom+Runtime::kDisplayHiddenRows+1,fullHeight})}};
}

Render::TerminalSize required_game_terminal_size(Runtime::GameContext& context,
    Runtime::GameContext* second, bool compact) {
    if (compact) return game_terminal_requirements(context,second).full;
    auto required=Render::board_terminal_size(context,context.session.player);
    const auto merge=[&](Render::TerminalSize other) {
        required.columns=std::max(required.columns,other.columns); required.rows=std::max(required.rows,other.rows);
    };
    merge(replay_playback_terminal_size(context,context.session.player));
    if (second) {
        merge(Render::board_terminal_size(*second,second->session.player));
        merge(replay_playback_terminal_size(*second,second->session.player));
    } else if (!context.config.mode.replayMode && context.config.garbage.mode==Runtime::GarbageMode::Multiplayer)
        merge(Render::board_terminal_size(context,context.session.opponent));
    return {required.columns+2,required.rows+2};
}

void center_game_boards(Runtime::GameContext& context,Render::TerminalSize size,Runtime::GameContext* second) {
    if(context.session.roomScene.enabled) {position_multiplayer_scene(context,size);return;}
    const auto value=layout(context,second,false);
    auto revision=context.session.player.presentation.terminalLayoutRevision+1;
    if (auto* app=context.app_component()) {
        app->gameTerminalLayout=signature(value,size);
        revision=++app->gameTerminalLayoutRevision;
    }
    int dx=horizontal_translation(value,size.columns),dy=vertical_translation(value,size.rows);
    if(value.slots.size()==2) {
        const auto anchor=multiplayer_left_anchor(context,*value.slots.front().player,size);
        dy=anchor.row;if(value.columns>size.columns-2)dx=anchor.column-value.slots.front().offset;
    }
    position_layout(value,dx,dy,size,revision);
    for(const auto& slot:value.slots)slot.player->presentation.terminalPaintRevision=revision;
}

bool refresh_game_terminal_layout(Runtime::GameContext& context) {
    if(context.session.roomScene.enabled) {
        const auto size=Render::terminal_size();const auto& p=context.session.player.presentation;
        if(size && (p.terminalColumns!=size->columns || p.terminalRows!=size->rows || p.layoutBoardWidth!=context.session.player.gameplay.playfield.width ||
            p.layoutBoardHeight!=context.session.player.gameplay.playfield.visibleHeight || p.layoutNextCount!=context.session.player.gameplay.playfield.nextCount)) {
            position_multiplayer_scene(context,*size);draw_multiplayer_scene(context,true);return true;
        }
        return false;
    }
    if (Render::output_suppressed()) return false;
    Render::OutputGuard output(context);
    const auto size=Render::terminal_size();
    if (!size) return false;
    auto& p=context.session.player.presentation;
    auto* app=context.app_component();
    if (app) app->observe_terminal_size(size->columns,size->rows);
    if (p.terminalColumns==size->columns && p.terminalRows==size->rows &&
        p.layoutBoardWidth==context.session.player.gameplay.playfield.width &&
        p.layoutBoardHeight==context.session.player.gameplay.playfield.visibleHeight &&
        p.layoutNextCount==context.session.player.gameplay.playfield.nextCount &&
        p.terminalPaintRevision==p.terminalLayoutRevision &&
        (!app || p.terminalLayoutRevision==app->gameTerminalLayoutRevision)) return false;
    auto& primary=app && app->primaryContext ? *app->primaryContext : context;
    auto* second=local_second(primary);
    const auto value=layout(primary,second,false);
    const auto sig=signature(value,*size);
    auto revision=p.terminalLayoutRevision+1;
    if (!app || app->gameTerminalLayout!=sig) {
        Render::clear_screen();
        if (app) { app->gameTerminalLayout=sig; ++app->gameTerminalLayoutRevision; }
    }
    if (app) revision=app->gameTerminalLayoutRevision;
    int dx=horizontal_translation(value,size->columns),dy=vertical_translation(value,size->rows);
    if(value.slots.size()==2) {
        const auto anchor=multiplayer_left_anchor(primary,*value.slots.front().player,*size);
        dy=anchor.row;if(value.columns>size->columns-2)dx=anchor.column-value.slots.front().offset;
    }
    // Move both workers' render coordinates atomically under the shared lock.
    // Their next incremental draw cannot paint at the old position; each worker
    // still owns the full redraw of its gameplay state.
    position_layout(value,dx,dy,*size,revision);
    for (const auto& slot:value.slots) {
        if (second && slot.context!=&context) continue;
        Render::redraw_player_board(*slot.context,*slot.player);
        GarbageService::redraw_buffer(*slot.context,*slot.player);
        if (slot.player==&slot.context->session.player)
            render_replay_playback_progress(*slot.context,*slot.player);
        slot.player->presentation.terminalPaintRevision=revision;
    }
    return true;
}
}
