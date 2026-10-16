#include "tetr/engine/multiplayer_scene.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/util/terminal_text.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

namespace Engine {
namespace {
bool local_input_pending(Runtime::GameContext& c) {
    auto* input=c.input_component();
    if(!input || !input->gameplay_input_active())return false;
    return input->gameplay_input_pending();
}
Runtime::PlayerSessionState* board(Runtime::GameContext& c,int slot) {
    if(slot==c.session.roomScene.ownSlot) return &c.session.player;
    auto it=c.session.roomScene.players.find(slot);return it==c.session.roomScene.players.end()?nullptr:&it->second;
}
std::pair<int,int> mini_dimensions(const Runtime::PlayerSessionState& player,int cellWidth,int cellHeight) {
    const int width=player.gameplay.playfield.width*2,height=player.gameplay.playfield.visibleHeight;
    const int availableWidth=std::max(1,cellWidth-3),availableHeight=std::max(1,cellHeight-3);
    const double scale=std::max(1.,double(width)/availableWidth);
    return {std::max(1,int(std::floor(width/scale))),std::max(1,std::min(availableHeight,int(std::ceil(height/scale))))};
}
void center_dynamic_scene(Runtime::GameContext& c,Render::TerminalSize size) {
    auto& s=c.session.roomScene;const bool battle=!s.spectator && s.selfAlive;
    const bool finalTwo=s.survivors.size()==2 || (s.survivors.size()<2 && s.finalists.size()==2);
    if(battle && !finalTwo)return;
    if(battle && s.finalBattleAnchor) {
        const auto& a=*s.finalBattleAnchor;const auto& p=c.session.player.gameplay.playfield;
        if(a.columns==size.columns && a.rows==size.rows && a.width==p.width && a.height==p.visibleHeight)return;
    }
    double x=0,y=0,weight=0;int left=size.columns,right=0,top=size.rows,bottom=0;
    const auto mass=[&](double a,double b,double w,double h,double density) {
        const double value=w*h*density;if(value<=0)return;
        x+=(a+w/2)*value;y+=(b+h/2)*value;weight+=value;
    };
    const auto full=full_multiplayer_slots(c,size);
    for(int slot:full)if(auto* p=board(c,slot)) {
        const auto bounds=Render::board_horizontal_extent(c,*p,false);const auto vertical=Render::board_vertical_extent(c,*p,false);
        const int width=p->gameplay.playfield.width*2,height=p->gameplay.playfield.visibleHeight;
        const auto& v=p->presentation;
        // The wider left extent reserves room for erasing transient text.
        // Counting that empty space as visible content pins a duel against
        // the right edge in smaller terminals. Balance the actual HUD/HOLD
        // footprint while keeping the playfields and NEXT panels in view.
        const int panelWidth=std::max(14,p->gameplay.playfield.holdEnabled?
            (v.blockMaxWidth+1)*2+2+2*Runtime::uses_garbage_buffer(c.config.garbage.mode):0);
        const int visibleLeft=v.margin-panelWidth;
        left=std::min(left,visibleLeft);right=std::max(right,bounds.right);
        top=std::min(top,v.topMargin-Runtime::kDisplayHiddenRows);bottom=std::max(bottom,v.topMargin+vertical.fullBottom);
        mass(v.margin,v.topMargin,width,height,2);
        mass(visibleLeft,v.topMargin,panelWidth,v.hold_panel_height()+1,.5);
        mass(visibleLeft,v.topMargin+Render::board_info_top_row(*p),panelWidth,15,.5);
        if(p->gameplay.playfield.nextCount>0)mass(v.margin+width+1,v.topMargin,bounds.right-v.margin-width-1,
            v.next_panel_height(p->gameplay.playfield.nextCount),1);
    }
    for(std::size_t i=0;i<s.miniSlots.size();++i) {
        const auto& g=s.miniLayouts.at(s.miniSlots[i]);
        const int cellLeft=s.galleryColumn+int(i)%s.miniColumns*s.miniCellWidth;
        left=std::min(left,cellLeft);right=std::max(right,cellLeft+s.miniCellWidth-1);
        top=std::min(top,g.ceiling);bottom=std::max(bottom,g.top+g.height+3);
        mass(g.left+1,g.top+1,g.width,g.height,1);
    }
    if(!weight)return;
    int dx=0,dy=0;
    if(right-left+2<=size.columns)dx=std::clamp(int(std::lround(size.columns/2.-x/weight)),1-left,size.columns-right-1);
    if(!(battle && finalTwo) && bottom-top+2<=size.rows)dy=std::clamp(int(std::lround(size.rows/2.-y/weight)),1-top,size.rows-bottom-1);
    for(int slot:full)if(auto* p=board(c,slot)){p->presentation.margin+=dx;p->presentation.topMargin+=dy;}
    s.galleryColumn+=dx;s.galleryTop+=dy;
    for(auto& [slot,g]:s.miniLayouts){g.left+=dx;g.top+=dy;if(g.ceiling>0)g.ceiling+=dy;}
    if(battle) {
        const auto& p=c.session.player;
        s.finalBattleAnchor=Runtime::SessionState::RoomScene::BattleAnchor{p.presentation.margin,p.presentation.topMargin,
            size.columns,size.rows,p.gameplay.playfield.width,p.gameplay.playfield.visibleHeight};
    }
}
void text_at(int row,int col,const std::string& text,int width,Render::TerminalSize size) {
    if(row<0 || row>=size.rows || col>=size.columns || width<=0) return;
    const auto clipped=Util::TerminalText::slice(text,std::max(0,-col),std::min(width,size.columns-std::max(0,col)));
    Render::move_cursor(row,std::max(0,col));std::cout<<clipped;
}
void name_under(Runtime::GameContext& c,Runtime::PlayerSessionState& p,int slot,const std::string& result) {
    const auto& name=c.session.roomScene.usernames[slot];
    p.presentation.username=result.empty()?name:compact_player_name(name,p.gameplay.playfield.width*2-int(result.size())-1)+" "+result;
    Render::print_score(c,p);p.presentation.username=name;
}
void paint_remote(Runtime::GameContext& c,Runtime::PlayerSessionState& current,
    Runtime::PlayerSessionState& previous) {
    using namespace Render;
    auto& field=current.gameplay.playfield;
    const bool visibilityChanged=synchronize_field_visibility(c,current);
    bool cellsChanged=visibilityChanged;
    for(int row=-Runtime::kDisplayHiddenRows;row<field.visibleHeight;++row)
        for(int col=0;col<field.width;++col)
            cellsChanged|=field.field.cell(row,col)!=previous.gameplay.playfield.field.cell(row,col) ||
                field.field.cell_lock_time(row,col)!=previous.gameplay.playfield.field.cell_lock_time(row,col);
    const bool activeChanged=cellsChanged || current.presentation.activePose!=previous.presentation.activePose ||
        current.presentation.activePieceType!=previous.presentation.activePieceType;
    const bool transition=activeChanged && !cellsChanged && current.presentation.activePose && previous.presentation.activePose &&
        current.presentation.activePieceType && current.presentation.activePieceType==previous.presentation.activePieceType;
    if(transition) {
        const auto piece=Piece::get_definition(*current.presentation.activePieceType);
        draw_piece_transition(c,piece,current,*previous.presentation.activePose,*current.presentation.activePose,field.field);
    } else if(activeChanged && previous.presentation.activePose && previous.presentation.activePieceType)
        draw_board_piece(c,previous,*previous.presentation.activePieceType,*previous.presentation.activePose,PieceDrawMode::Erase);
    if(previous.gameplay.hold.piece!=current.gameplay.hold.piece ||
        hold_draw_mode(c,previous)!=hold_draw_mode(c,current)) {
        draw_board_hold(c,previous,PieceDrawMode::Erase);draw_board_hold(c,current);
    }
    if(previous.pieces.nextPreview!=current.pieces.nextPreview)
        draw_next_preview_transition(c,current,previous.pieces.nextPreview);
    for(int row=-Runtime::kDisplayHiddenRows;row<field.visibleHeight;++row)
        for(int col=0;col<field.width;++col)
            if(visibilityChanged || field.field.cell(row,col)!=previous.gameplay.playfield.field.cell(row,col) ||
                field.field.cell_lock_time(row,col)!=previous.gameplay.playfield.field.cell_lock_time(row,col))
                draw_board_cell(c,current,row,col,field.field.cell(row,col));
    if(activeChanged && !transition && current.presentation.activePose && current.presentation.activePieceType && current.presentation.activePieceType->valid())
        draw_board_piece(c,current,*current.presentation.activePieceType,*current.presentation.activePose);
    const auto& garbage=current.presentation.garbageBufferDisplay;
    const auto& oldGarbage=previous.presentation.garbageBufferDisplay;
    for(std::size_t i=0;i<std::max(garbage.size(),oldGarbage.size());++i) {
        const int cell=i<garbage.size()?garbage[i]:0,old=i<oldGarbage.size()?oldGarbage[i]:0;
        if(cell==old) continue;
        move_to(c,current,field.visibleHeight-static_cast<int>(i)-1,0,-3);
        set_ui_color(cell==1?ColorRole::IncomingGarbage:cell==2?ColorRole::Danger:ColorRole::Accent,c.config.display.obscuredColors);
        draw_square(c.config.display.squareStyle,cell?SquareFill::Filled:SquareFill::Empty);
    }
    // Preserve the widths of the text actually on screen for proper erasure.
    current.presentation.messageWidths=previous.presentation.messageWidths;
    current.presentation.messageText=previous.presentation.messageText;
    current.presentation.messageMargin=previous.presentation.messageMargin;
    current.presentation.messageTopMargin=previous.presentation.messageTopMargin;
    current.presentation.messageObscured=previous.presentation.messageObscured;
    current.presentation.lastMessagePaint=previous.presentation.lastMessagePaint;
    print_message(c,current);
    if(current.gameplay.scoring.b2b!=previous.gameplay.scoring.b2b) print_b2b(c,current);
    if(current.gameplay.scoring.combo!=previous.gameplay.scoring.combo) print_combo(c,current);
    if(current.presentation.remotePlacementRevision!=previous.presentation.remotePlacementRevision) {
        const auto& p=current.presentation;
        print_spin(c,current,p.remoteSpinPiece,p.remoteMini);print_clear(c,current,p.remoteClearLines);
        print_all_clear(c,current,p.remotePerfectClear,p.remoteHalfClear);
    }
    if(activeChanged || current.presentation.nextSpawnType!=previous.presentation.nextSpawnType ||
       current.presentation.nextSpawnPose!=previous.presentation.nextSpawnPose ||
       current.pieces.nextPreview!=previous.pieces.nextPreview) refresh_spawn_marker(c,current);
    previous=current;
}
}
std::string compact_player_name(std::string_view name,int columns) {
    if(columns<=0) return {};
    const auto width=Util::TerminalText::width(name);
    if(width<=static_cast<std::size_t>(columns)) return std::string(name);
    if(columns<=3) return std::string(columns,'.');
    const int left=(columns-3+1)/2,right=columns-3-left;
    return Util::TerminalText::slice(name,0,left)+"..."+Util::TerminalText::slice(name,width-right,right);
}
void initialize_multiplayer_scene(Runtime::GameContext& c) {
    auto& n=Network::network_services(c);auto& s=c.session.roomScene;s={};
    s.enabled=n.multiplayerRoom;s.ownSlot=n.roomPlayerIndex;s.epoch=n.matchEpoch;
    update_multiplayer_scene(c);
}
void update_multiplayer_scene(Runtime::GameContext& c) {
    auto& n=Network::network_services(c);auto& s=c.session.roomScene;
    if(!s.enabled || n.matchEpoch!=s.epoch) return;
    s.finished=n.matchPhase==Network::RoomMatchPhase::Finished;
    std::vector<int> survivors;
    bool spectator=s.spectator,alive=s.selfAlive;
    for(const auto& m:n.roomMembers) {
        s.usernames[m.slot]=m.username;s.killers[m.slot]=m.killer;
        if(m.slot==s.ownSlot) {spectator=!m.participating;alive=m.alive;}
        if(!(s.localForfeit && m.slot==s.ownSlot) &&
           ((n.matchPhase==Network::RoomMatchPhase::Preparing && m.participating) || m.alive)) survivors.push_back(m.slot);
    }
    for(const auto& [slot,setup]:n.playerSetups) {
        if(!s.players.count(slot)) ++s.revision;
        auto [it,created]=s.players.try_emplace(slot);
        if(created) {
            auto& p=it->second;p.gameplay.playfield.width=setup.width;p.gameplay.playfield.visibleHeight=setup.visibleHeight;
            p.gameplay.playfield.activeHeight=setup.visibleHeight;p.gameplay.playfield.nextCount=setup.nextCount;
            p.gameplay.playfield.holdEnabled=setup.holdEnabled;p.presentation.blockMaxWidth=setup.blockMaxWidth;
            p.gameplay.scoring.wins=setup.wins;
            p.presentation.blockMaxHeight=setup.blockMaxHeight;p.pieces.nextPreview=setup.nextPreview;
            p.gameplay.playfield.field.initialize(setup.width,setup.visibleHeight);
        }
        auto& p=it->second;
        if(p.gameplay.playfield.width!=setup.width || p.gameplay.playfield.visibleHeight!=setup.visibleHeight) {
            p.gameplay.playfield.width=setup.width;p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=setup.visibleHeight;
            p.gameplay.playfield.field.initialize(setup.width,setup.visibleHeight);++s.revision;s.lastPaint={};
        }
        p.gameplay.playfield.nextCount=setup.nextCount;p.gameplay.playfield.holdEnabled=setup.holdEnabled;
        p.presentation.blockMaxWidth=setup.blockMaxWidth;p.presentation.blockMaxHeight=setup.blockMaxHeight;
        p.presentation.username=s.usernames[slot];
        if(const auto mini=n.peerMiniBoards.find(slot);mini!=n.peerMiniBoards.end() && mini->second.clock &&
            s.appliedMiniClocks[slot]!=mini->second.receivedRevision) {
            apply_remote_clock_sample(c,p,*mini->second.clock);s.appliedMiniClocks[slot]=mini->second.receivedRevision;
        }
    }
    c.session.player.presentation.username=s.usernames[s.ownSlot];
    if(n.matchPhase==Network::RoomMatchPhase::Preparing) alive=!spectator;
    alive=alive && !s.localForfeit;
    const bool newlyDead=s.selfAlive && !alive && !spectator;
    s.selfAlive=alive;s.spectator=spectator;
    int focus=newlyDead?s.killers[s.ownSlot]:s.focusSlot;
    if(!s.spectator && s.selfAlive) focus=s.ownSlot;
    if(std::find(survivors.begin(),survivors.end(),focus)==survivors.end()) {
        std::set<int> seen;
        while(focus>=0 && seen.insert(focus).second && s.killers.count(focus)) {
            focus=s.killers[focus];if(std::find(survivors.begin(),survivors.end(),focus)!=survivors.end()) break;
        }
        if(std::find(survivors.begin(),survivors.end(),focus)==survivors.end())
            focus=survivors.empty()?-1:survivors[c.services.random()%survivors.size()];
    }
    if(survivors.size()==2 && s.finalists!=survivors) {
        s.finalists=survivors;s.finalistLayout=survivors;
        const int main=(!spectator && alive)?s.ownSlot:focus;
        if(s.finalistLayout.back()==main) std::swap(s.finalistLayout.front(),s.finalistLayout.back());
    }
    for(const auto& member:n.roomMembers) {
        auto* p=board(c,member.slot);if(!p) continue;
        if(n.matchPhase!=Network::RoomMatchPhase::Preparing && member.participating && (!member.alive || s.finished)) {
            if(!p->timing.completedGameTime) {
                p->timing.completedGameTime=p->timing.remoteGameTime?p->timing.remote_elapsed(c.services.clock.now()):
                    Runtime::elapsed_game_time(c.session,c.now())*c.config.timeMultiplier;
                p->timing.completedRealTime=p->timing.realGameStart ? c.real_now()-*p->timing.realGameStart : *p->timing.completedGameTime;
            }
            if(!member.alive) {p->presentation.activePose.reset();p->presentation.activePieceType.reset();}
        }
    }
    if(s.survivors!=survivors || s.focusSlot!=focus) {s.survivors=survivors;s.focusSlot=focus;++s.revision;s.lastPaint={};}
    auto& turns=n.pieceTurns;turns.participants=survivors;turns.peers=n.peerTurns;
    turns.enabled=c.config.turnPieces>0 && !spectator && alive;
    for(int peer:survivors) if(peer!=s.ownSlot) {
        auto setup=n.playerSetups.find(peer);if(setup!=n.playerSetups.end() && setup->second.turnPieces<=0) turns.enabled=false;
    }
}
void cycle_multiplayer_focus(Runtime::GameContext& c) {
    auto& s=c.session.roomScene;if(!s.spectator && s.selfAlive) return;
    if(s.survivors.empty()) return;
    auto it=std::find(s.survivors.begin(),s.survivors.end(),s.focusSlot);
    s.focusSlot=it==s.survivors.end() || ++it==s.survivors.end()?s.survivors.front():*it;
    if(s.survivors.size()==2) {
        s.finalistLayout=s.survivors;
        if(s.finalistLayout.back()==s.focusSlot)std::swap(s.finalistLayout.front(),s.finalistLayout.back());
    }
    ++s.revision;s.lastPaint={};draw_multiplayer_scene(c,true);
}
void position_multiplayer_scene(Runtime::GameContext& c,Render::TerminalSize size) {
    auto& s=c.session.roomScene;
    const auto full=full_multiplayer_slots(c,size);
    struct Positioned {Runtime::PlayerSessionState* player;int offset;};
    std::vector<Positioned> positioned;int width=0;
    for(int slot:full)if(auto* p=board(c,slot)) {
        const auto bounds=Render::board_horizontal_extent(c,*p,false);
        const int left=bounds.left-p->presentation.margin,right=bounds.right-p->presentation.margin;
        const int offset=width+(positioned.empty()?0:4)-left;
        positioned.push_back({p,offset});width=offset+right;
    }
    s.miniSlots.clear();s.miniLayouts.clear();s.miniColumns=s.miniCellWidth=s.miniCellHeight=0;
    if(positioned.empty())return;
    const auto anchor=multiplayer_left_anchor(c,*positioned.front().player,size);
    const int dx=anchor.column-positioned.front().offset,dy=anchor.row;
    const int mainBottom=dy+positioned.front().player->gameplay.playfield.visibleHeight;
    for(const auto& value:positioned) {
        auto& p=value.player->presentation;p.margin=dx+value.offset;p.topMargin=mainBottom-value.player->gameplay.playfield.visibleHeight;
        p.dynamicLayout=true;p.terminalColumns=size.columns;p.terminalRows=size.rows;
        p.layoutBoardWidth=value.player->gameplay.playfield.width;p.layoutBoardHeight=value.player->gameplay.playfield.visibleHeight;
        p.layoutNextCount=value.player->gameplay.playfield.nextCount;
    }
    if(positioned.size()==2)positioned.back().player->presentation.topMargin=aligned_opponent_top(
        *positioned.front().player,*positioned.back().player,dy,size.rows);
    std::vector<int> small;int naturalWidth=12,naturalHeight=9;
    const auto& candidates=s.survivors.size()<2 && s.finalists.size()==2?s.finalists:s.survivors;
    for(int slot:candidates)if(std::find(full.begin(),full.end(),slot)==full.end())if(auto* p=board(c,slot)) {
        small.push_back(slot);naturalWidth=std::max(naturalWidth,p->gameplay.playfield.width*2+3);
        naturalHeight=std::max(naturalHeight,p->gameplay.playfield.visibleHeight+3);
    }
    s.galleryColumn=dx+width+4;
    const int available=std::max(0,size.columns-s.galleryColumn-2),height=std::max(0,size.rows-2);
    const int minimumMiniWidth=full.size()==1 && candidates.size()==2?6:12;
    const int capacity=(available/minimumMiniWidth)*(height/9);
    if(int(small.size())>capacity) {
        const int target=Network::network_services(c).targetSlot;
        if(const auto found=std::find(small.begin(),small.end(),target);found!=small.end())std::rotate(small.begin(),found,found+1);
        small.resize(capacity);
    }
    int bestArea=0,bestRows=0,galleryWidth=0,galleryHeight=0;bool bestReadable=false;
    for(int columns=1;columns<=int(small.size());++columns) {
        const int rows=(int(small.size())+columns-1)/columns;
        const int cellW=std::min(available/columns,naturalWidth);
        const int cellH=std::min((rows==1?std::min(height,std::max(0,mainBottom+2)):height)/rows,naturalHeight);
        if(cellW<minimumMiniWidth || cellH<9)continue;
        int area=0,usedWidth=0,usedHeight=0;
        for(std::size_t i=0;i<small.size();++i) {
            const auto [w,h]=mini_dimensions(*board(c,small[i]),cellW,cellH);
            area+=w*h;usedWidth=std::max(usedWidth,int(i)%columns*cellW+cellW-1);
            usedHeight=std::max(usedHeight,(int(i)/columns+1)*cellH-1);
        }
        const bool readable=cellW>=std::min(16,naturalWidth);
        if(!bestArea || readable>bestReadable || (readable==bestReadable &&
            (rows<bestRows || (rows==bestRows && (columns>s.miniColumns || (columns==s.miniColumns && area>bestArea)))))) {
            bestArea=area;bestRows=rows;bestReadable=readable;s.miniColumns=columns;s.miniCellWidth=cellW;s.miniCellHeight=cellH;
            galleryWidth=usedWidth;galleryHeight=usedHeight;
        }
    }
    if(!bestArea){center_dynamic_scene(c,size);return;}
    s.miniSlots=std::move(small);const bool single=s.miniSlots.size()<=std::size_t(s.miniColumns);
    s.galleryTop=std::max(0,(size.rows-galleryHeight)/2);
    if(single) {
        int tallest=0;for(int slot:s.miniSlots)tallest=std::max(tallest,mini_dimensions(*board(c,slot),s.miniCellWidth,s.miniCellHeight).second);
        s.galleryTop=mainBottom-tallest-1;
    }
    for(std::size_t i=0;i<s.miniSlots.size();++i) {
        auto* p=board(c,s.miniSlots[i]);const auto [w,h]=mini_dimensions(*p,s.miniCellWidth,s.miniCellHeight);
        const int top=single?mainBottom-h-1:s.galleryTop+(int(i)/s.miniColumns+1)*s.miniCellHeight-h-3;
        int ceiling=0;
        if(i>=std::size_t(s.miniColumns)) {
            const auto& above=s.miniLayouts.at(s.miniSlots[i-s.miniColumns]);ceiling=above.top+above.height+3;
        }
        s.miniLayouts[s.miniSlots[i]]={s.galleryColumn+int(i)%s.miniColumns*s.miniCellWidth+(s.miniCellWidth-w-3)/2,top,w,h,ceiling,
            p->gameplay.playfield.width,p->gameplay.playfield.visibleHeight};
    }
    center_dynamic_scene(c,size);
}
void draw_multiplayer_scene(Runtime::GameContext& c,bool force,std::optional<Render::TerminalSize> sizeOverride) {
    auto& s=c.session.roomScene;if(!s.enabled || Render::output_suppressed()) return;
    const auto now=c.real_now();const auto renderInterval=Runtime::Duration::from_seconds(1.0/120.0);
    if(!force && (now-s.lastPaint<renderInterval || local_input_pending(c))) return;
    const auto size=sizeOverride.value_or(Render::terminal_size().value_or(Render::TerminalSize{120,40}));
    if(sizeOverride) {
        auto& viewport=c.session.player.presentation;viewport.dynamicLayout=true;viewport.terminalColumns=size.columns;viewport.terminalRows=size.rows;
    }
    Render::OutputGuard output(c,true);
    const auto full=full_multiplayer_slots(c,size);
    if(full!=s.paintedFullSlots)force=true;
    if(!s.miniSlots.empty() && Network::network_services(c).targetSlot>=0 && s.miniSlots.size()+full.size()<s.survivors.size() &&
        std::find(full.begin(),full.end(),Network::network_services(c).targetSlot)==full.end() &&
        std::find(s.miniSlots.begin(),s.miniSlots.end(),Network::network_services(c).targetSlot)==s.miniSlots.end())force=true;
    if(size.columns!=s.paintedColumns || size.rows!=s.paintedRows) force=true;
    for(const auto& [slot,geometry]:s.miniLayouts)if(auto* p=board(c,slot))
        if(p->gameplay.playfield.width!=geometry.logicalWidth || p->gameplay.playfield.visibleHeight!=geometry.logicalHeight)force=true;
    for(int slot:full) if(auto* p=board(c,slot))
        if(p->presentation.layoutBoardWidth!=p->gameplay.playfield.width || p->presentation.layoutBoardHeight!=p->gameplay.playfield.visibleHeight ||
           p->presentation.layoutNextCount!=p->gameplay.playfield.nextCount) force=true;
    for(int slot:full) if(auto* p=board(c,slot)) if(const auto it=s.paintedPlayers.find(slot);it!=s.paintedPlayers.end()) {
        const auto& previous=it->second;
        if(previous.gameplay.playfield.holdEnabled!=p->gameplay.playfield.holdEnabled ||
           previous.presentation.blockMaxWidth!=p->presentation.blockMaxWidth || previous.presentation.blockMaxHeight!=p->presentation.blockMaxHeight) force=true;
    }
    if(force || s.lastPaint==Runtime::TimePoint{}) {
        Render::clear_screen();position_multiplayer_scene(c,size);s.paintedPlayers.clear();s.paintedMiniSequences.clear();s.paintedMiniCells.clear();s.miniPaintCursor=0;s.paintedFullSlots=full;force=true;
    }
    Network::BoardViews views;views.remaining=int(s.survivors.size());views.highRate=true;int pixels=0;
    for(int slot:full)if(slot!=s.ownSlot)views.players.push_back({slot,true,0,0,120});
    for(int slot:s.miniSlots) {
        const auto& g=s.miniLayouts.at(slot);const int rows=std::clamp(g.top+g.height+1-g.ceiling,1,160);
        views.players.push_back({slot,false,g.width,rows,1});pixels+=g.width*rows;
    }
    // Budget visible pixels, rather than imposing a low rate on every
    // multiplayer room. Idle boards still send only repair heartbeats.
    const int miniCount=int(s.miniSlots.size());
    const int fps=std::clamp(std::min(60000/std::max(1,pixels),Network::network_services(c).miniUpdateBudget/std::max(1,miniCount)),4,60);
    for(auto& view:views.players)if(!view.full)view.fps=fps;
    Network::publish_board_views(c,views);
    for(const int slot:full) if(auto* p=board(c,slot)) {
        if(force || (slot!=s.ownSlot && !s.paintedPlayers.count(slot))) {
            synchronize_field_visibility(c,*p);
            Render::redraw_player_board(c,*p);
            // Paint the complete garbage meter once, using an empty previous
            // meter; subsequent updates touch only its changed cells.
            if(slot!=s.ownSlot) {
                auto previous=*p;previous.presentation.garbageBufferDisplay.clear();previous.presentation.remotePlacementRevision=0;
                paint_remote(c,*p,previous);
                s.paintedPlayers[slot]=*p;
            }
        } else if(slot!=s.ownSlot) {
            paint_remote(c,*p,s.paintedPlayers.at(slot));
        }
        std::string result;
        for(const auto& member:Network::network_services(c).roomMembers) if(member.slot==slot && member.participating) {
            if(s.finished) result=slot==Network::network_services(c).winnerSlot?"WIN":"LOSE";
            else if(!member.alive) result="LOSE";
        }
        if(force || !s.paintedNames.count(slot) || s.paintedNames[slot]!=s.usernames[slot] || s.paintedResults[slot]!=result) {
            name_under(c,*p,slot,result);s.paintedNames[slot]=s.usernames[slot];
        }
        s.paintedResults[slot]=result;
    }
    const auto& small=s.miniSlots;
    const std::size_t perBatch=std::clamp<std::size_t>((small.size()*fps+119)/120,1,4);
    const int miniBatches=int((small.size()+perBatch-1)/perBatch);
    const auto miniInterval=Runtime::Duration::from_seconds(1./(fps*std::max(1,miniBatches)));
    const bool legacyDue=force || now-s.lastMiniPaint>=miniInterval;
    if(!small.empty()) {
        if(legacyDue) {
            if(force || s.lastMiniPaint==Runtime::TimePoint{} || now-s.lastMiniPaint>=miniInterval*2.)s.lastMiniPaint=now;
            else s.lastMiniPaint+=miniInterval;
        }
        const int cellW=s.miniCellWidth;
        const std::size_t start=force?0:s.miniPaintCursor;
        const auto paintDeadline=std::chrono::steady_clock::now()+std::chrono::microseconds(500);
        std::size_t painted=0,legacyPainted=0;
        // Network subscriptions already pace incoming images. Paint fresh
        // cached frames immediately instead of imposing a second low-rate
        // sampling delay, skipping unchanged peers without spending a slot.
        for(std::size_t step=0;step<small.size() && (force || painted<4);++step) {
            if(!force && ((step && std::chrono::steady_clock::now()>=paintDeadline) || local_input_pending(c)))break;
            const std::size_t index=(start+step)%small.size();
            s.miniPaintCursor=(index+1)%small.size();
            auto* p=board(c,small[index]);if(!p) continue;
            const auto& geometry=s.miniLayouts.at(small[index]);
            const int w=geometry.width,h=geometry.height,left=geometry.left,top=geometry.top,ceiling=geometry.ceiling;
            const double scale=std::max(1.,double(p->gameplay.playfield.width*2)/w);
            const int first=std::max(ceiling-top-1,h-int(std::ceil((p->gameplay.playfield.visibleHeight+Runtime::kHiddenRowCapacity)/scale)));
            const auto sourceRow=[&](int row) {
                return p->gameplay.playfield.visibleHeight+int(std::floor((row-h)*scale));
            };
            std::optional<Network::MiniBoardFrame> miniature;
            const auto cached=Network::network_services(c).peerMiniBoards.find(small[index]);
            const bool nameChanged=!s.paintedNames.count(small[index]) || s.paintedNames.at(small[index])!=s.usernames[small[index]];
            if(cached!=Network::network_services(c).peerMiniBoards.end() && !force && !nameChanged &&
                s.paintedMiniSequences.count(small[index]) && s.paintedMiniSequences.at(small[index])==cached->second.sequence)continue;
            if(cached==Network::network_services(c).peerMiniBoards.end()) {
                if(!force && (!legacyDue || legacyPainted>=perBatch))continue;
                ++legacyPainted;
            }
            ++painted;
            if(cached!=Network::network_services(c).peerMiniBoards.end() && cached->second.logicalWidth==p->gameplay.playfield.width &&
                cached->second.logicalHeight==p->gameplay.playfield.visibleHeight)
                miniature=Network::project_mini_board(cached->second,w,std::clamp(h-first,1,160));
            if(miniature)s.paintedMiniSequences[small[index]]=miniature->sequence;
            auto& previous=s.paintedMiniCells[small[index]];const bool initial=force || previous.size()!=std::size_t(w*(h-first));
            if(initial)previous.assign(w*(h-first),Runtime::BoardCell{});
            // Open top: fixed cells may rise into the free space above this
            // card, but never overwrite the preceding player's card/name.
            // Repaint empty overflow cells too, so shrinking stacks erase.
            std::string run;run.reserve(w*3);
            for(int y=first;y<h;++y) {
                if(initial){Render::set_ui_color(Render::ColorRole::Text,c.config.display.obscuredColors);text_at(top+y+1,left,y>=0?"|":" ",1,size);}
                int runStart=-1,runWidth=0;Runtime::BoardCell runCell;
                const auto flushRun=[&] {
                    if(runStart<0)return;
                    Render::set_board_cell_color(runCell,c.config.display.obscuredColors);
                    text_at(top+y+1,left+runStart+1,run,runWidth,size);
                    runStart=-1;runWidth=0;run.clear();
                };
                for(int x=0;x<w;++x) {
                    Runtime::BoardCell cell;
                    // Aggregate fixed cells only. NEXT, HOLD and the falling piece never enter this projection.
                    const int r0=std::max(-Runtime::kHiddenRowCapacity,sourceRow(y));
                    const int r1=std::min(p->gameplay.playfield.visibleHeight,std::max(r0+1,sourceRow(y+1)));
                    const int c0=x*p->gameplay.playfield.width/w,c1=std::max(c0+1,(x+1)*p->gameplay.playfield.width/w);
                    if(miniature)cell=miniature->at(miniature->rows+y-h,x);
                    else for(int row=r0;row<r1;++row) for(int col=c0;col<c1;++col) {
                            const auto candidate=p->gameplay.playfield.field.cell(row,col);if(candidate.occupied()) cell=candidate;
                        }
                    auto& old=previous[(y-first)*w+x];if(!initial && old==cell){flushRun();continue;}old=cell;
                    if(runStart>=0 && runCell!=cell)flushRun();
                    if(runStart<0){runStart=x;runCell=cell;}
                    run+=cell.occupied()?"█":" ";++runWidth;
                }
                flushRun();
                if(initial){Render::set_ui_color(Render::ColorRole::Text,c.config.display.obscuredColors);text_at(top+y+1,left+w+1,y>=0?"|":" ",1,size);}
            }
            Render::set_ui_color(Render::ColorRole::Text,c.config.display.obscuredColors);
            if(initial)text_at(top+h+1,left,"+"+std::string(w,'-')+"+",w+2,size);
            const int labelWidth=cellW-1;
            const auto label=compact_player_name(s.usernames[small[index]],labelWidth);
            if(initial || nameChanged) {
                Render::set_ui_color(Render::ColorRole::Accent,c.config.display.obscuredColors);
                const int padding=labelWidth-int(Util::TerminalText::width(label));
                const int cellLeft=s.galleryColumn+int(index)%s.miniColumns*cellW;
                text_at(top+h+2,cellLeft,std::string(padding/2,' ')+label+std::string(padding-padding/2,' '),labelWidth,size);
                s.paintedNames[small[index]]=s.usernames[small[index]];
            }
        }
    }
    // Keep the refresh phase: resetting to each late callback's arrival time
    // makes the next slightly early callback skip a whole display frame.
    if(force || s.lastPaint==Runtime::TimePoint{} || now-s.lastPaint>=renderInterval*2.)s.lastPaint=now;
    else s.lastPaint+=renderInterval;
    s.paintedColumns=size.columns;s.paintedRows=size.rows;Render::flush();
}
void poll_multiplayer_scene(Runtime::GameContext& c,bool receiveAttacks) {
    if(local_input_pending(c))return;
    const auto receiveDeadline=std::chrono::steady_clock::now()+std::chrono::microseconds(750);
    auto& n=Network::network_services(c);Network::poll_connection(n,std::chrono::milliseconds(0),std::chrono::microseconds(250));
    if(n.matchEpoch!=c.session.roomScene.epoch) return;
    update_multiplayer_scene(c);Network::resend_piece_turn(c);
    int applied=0;
    while(++applied<=64 && std::chrono::steady_clock::now()<receiveDeadline) {
        if(local_input_pending(c))break;
        auto packet=Network::try_receive_multiplayer_frame(c,false,std::chrono::microseconds(250));if(!packet) break;
        auto* p=board(c,packet->sourceSlot);
        if(packet->opponentState && p) {n.peerSnapshots[packet->sourceSlot]=*packet->opponentState;apply_remote_board_state(c,*p,*packet->opponentState,false);}
        else if(packet->motion && p && !apply_remote_piece_motion(c,*p,*packet->motion,false)) Network::request_board_snapshot(c);
        else if(packet->type==Network::FramePacketType::Attack) {
            auto& local=c.session.player;auto& m=local.multiplayer;
            auto applied=Network::apply_frame_packet(*packet,{m.pendingSends,m.receiveId,local.gameplay.scoring.wins});
            if(receiveAttacks && applied.incomingAttack) {
                const auto count=local.garbage.buffer.size();
                GarbageService::add_buffer(c,local,local.gameplay.playfield.field,applied.incomingAttack);
                if(local.garbage.buffer.size()>count) local.garbage.buffer.back().set_source_slot(packet->sourceSlot);
            }
        }
    }
    update_multiplayer_scene(c);Network::send_board_visual_tick(c);draw_multiplayer_scene(c);
}
} // namespace Engine
