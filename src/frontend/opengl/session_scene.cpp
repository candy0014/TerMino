#include "session_scene.h"
#include "board_layout.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/presentation/context_view.h"
#include "tetr/network/network_services.h"
#include "tetr/runtime/game_context.h"
#include <algorithm>
namespace Graphical {
Presentation::BoardView capture_graphical_board(const Runtime::GameContext& c,
    const Runtime::PlayerSessionState& player,const BoardGeometry& geometry) {
    // NEXT can move up to preserve readable previews, never below field.top.
    // Capturing that upper bound also covers wider rendering viewports.
    const int rows=std::max(displayed_above_rows(geometry),top_preview_geometry(geometry,player.gameplay.playfield.field.width()).rows);
    return Presentation::capture_board_view(c,player,rows);
}
RoomBoards capture_room_boards(Runtime::GameContext& c) {
    RoomBoards result;const auto& scene=c.session.roomScene;
    if(!scene.enabled)return result;
    const auto& network=Network::network_services(c);
    const bool preparing=!scene.finished && (network.matchPhase==Network::RoomMatchPhase::Preparing || network.matchPhase==Network::RoomMatchPhase::Countdown);
    auto full=Engine::full_multiplayer_slots(c);
    if(full.empty()) {
        int main=scene.spectator?scene.focusSlot:scene.ownSlot;
        if(main<0)for(const auto& member:network.roomMembers)if(member.participating){main=member.slot;break;}
        if(main>=0)full.push_back(main);
    }
    result.ownBoardVisible=!scene.spectator && std::find(full.begin(),full.end(),scene.ownSlot)!=full.end();
    std::vector<FieldSize> fields;std::vector<int> fieldSlots;
    // Match scene_view's order: a visible own board precedes full peers,
    // including when a saved finalist layout lists the opponent first.
    if(result.ownBoardVisible) {
        fields.push_back({c.session.player.gameplay.playfield.field.width(),c.session.player.gameplay.playfield.visibleHeight});
        fieldSlots.push_back(scene.ownSlot);
    }
    for(int slot:full) {
        if(slot==scene.ownSlot && !scene.spectator)continue;
        const auto it=scene.players.find(slot);
        const auto* p=slot==scene.ownSlot?&c.session.player:it==scene.players.end()?nullptr:&it->second;
        if(p) {fields.push_back({p->gameplay.playfield.field.width(),p->gameplay.playfield.visibleHeight});fieldSlots.push_back(slot);}
    }
    const auto layout=multiplayer_geometry(fields,{});
    const auto geometry=[&](int slot) {
        const auto it=std::find(fieldSlots.begin(),fieldSlots.end(),slot);
        return it==fieldSlots.end()?BoardGeometry{}:layout.full[std::size_t(it-fieldSlots.begin())];
    };
    result.ownBoard=capture_graphical_board(c,c.session.player,geometry(scene.ownSlot));
    std::vector<int> slots=full;
    // miniSlots is a terminal layout/cache and can still be empty in countdown.
    for(const auto slot:scene.participants)
        if((preparing || (scene.alive.count(slot) && scene.alive.at(slot))) && std::find(slots.begin(),slots.end(),slot)==slots.end())slots.push_back(slot);
    if(scene.participants.empty())for(const auto& member:network.roomMembers)
        if(member.participating && (preparing || member.alive) && std::find(slots.begin(),slots.end(),member.slot)==slots.end())slots.push_back(member.slot);
    for(int slot:slots) {
        if(slot==scene.ownSlot && !scene.spectator)continue;
        const auto player=scene.players.find(slot);if(player==scene.players.end())continue;
        auto value=capture_graphical_board(c,player->second,geometry(slot));
        const bool small=std::find(full.begin(),full.end(),slot)==full.end();
        const auto sample=network.peerMiniBoards.find(slot);
        if(!small || preparing || sample==network.peerMiniBoards.end() ||
           (player->second.presentation.remoteFieldVisibility && player->second.presentation.remoteFieldVisibility->invisible)) {
            if(small && !preparing){value.active.reset();value.ghost.reset();}
            result.peers.push_back({slot,small,std::move(value)});continue;
        }
        const auto& mini=sample->second;
        value.width=mini.columns;value.bottom=mini.rows;value.firstRow=0;
        value.cells.clear();value.active.reset();value.ghost.reset();
        for(const auto& cell:mini.cells)value.cells.push_back({cell,cell.occupied()?Presentation::CellAppearance::Visible:Presentation::CellAppearance::Empty});
        result.peers.push_back({slot,true,std::move(value),mini.logicalWidth,mini.logicalHeight,
            std::max(1.,2.*mini.logicalWidth/std::max(1,mini.columns))});
    }
    return result;
}
}
