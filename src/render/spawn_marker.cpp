#include "tetr/render/board_renderer.h"

#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/construction.h"

#include <algorithm>
#include <iostream>

namespace Render {
namespace {
bool contains(const std::vector<Runtime::CellPosition>& cells,int row,int column) {
    return std::any_of(cells.begin(),cells.end(),[&](const auto& cell) {
        return cell.row==row && cell.col==column;
    });
}
bool covers(const Piece::Definition& piece,Piece::Pose pose,int row,int column) {
    const auto& cells=piece.cells(pose.rotation);
    return std::any_of(cells.begin(),cells.end(),[&](const auto& offset) {
        return pose.position.row+offset[0]==row && pose.position.col+offset[1]==column;
    });
}
}

bool spawn_marker_cell(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player,
    int row,int column) {
    return context.config.display.showSpawnPosition && player.gameplay.playfield.nextCount>0 &&
        !player.pieces.nextPreview.empty() && contains(player.presentation.spawnMarkerCells,row,column);
}
bool board_hint_cell(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player,int row,int column) {
    return spawn_marker_cell(context,player,row,column) || Runtime::construction_cell(context.config,player.gameplay.playfield.field,row,column);
}

void draw_empty_board_cell(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player,
    int row,int column) {
    OutputGuard output(context);
    move_to(context,player,row,column);
    if (board_hint_cell(context,player,row,column)) {
        set_rgb(64,68,74);
        std::cout<<(context.config.display.squareStyle==0 ? " ." : ". ");
    } else {
        set_ui_color(ColorRole::Ghost,context.config.display.obscuredColors);
        draw_square(context.config.display.squareStyle,SquareFill::Empty);
    }
}

void refresh_spawn_marker(const Runtime::GameContext& context,Runtime::PlayerSessionState& player) {
    OutputGuard output(context);
    auto& p=player.presentation;
    const auto& field=player.gameplay.playfield;
    auto affected=p.spawnMarkerCells;
    p.spawnMarkerCells.clear();
    if (context.config.display.showSpawnPosition && field.nextCount>0 && !player.pieces.nextPreview.empty()) {
        const auto type=p.nextSpawnType.value_or(player.pieces.nextPreview.front());
        if (type.valid()) {
            const auto& entry=Piece::catalog_entry(type);
            auto pose=p.nextSpawnPose.value_or(Piece::spawn_pose(field.width,type));
            if (!p.nextSpawnPose && Runtime::enabled(context.config.quickPlay.snowball))
                pose=Piece::fit_spawn_inside_walls(pose,entry.shapes[pose.rotation.index()],field.width);
            for (const auto& offset:entry.shapes[pose.rotation.index()]) {
                const int row=pose.position.row+offset[0],column=pose.position.col+offset[1];
                if (row < -Runtime::kDisplayHiddenRows || row>=field.visibleHeight || column<0 || column>=field.width)
                    continue;
                p.spawnMarkerCells.push_back({row,column});
                if (!contains(affected,row,column)) affected.push_back({row,column});
            }
        }
    }
    if (affected.empty()) return;
    std::optional<Piece::Definition> active;
    std::optional<Piece::Pose> ghost;
    if (p.paintedActivePose && p.paintedActivePieceType && p.paintedActivePieceType->valid()) {
        active=Piece::get_definition(*p.paintedActivePieceType);
        if (context.config.field.ghost) {
            ghost=*p.paintedActivePose;
            while (active->can_place(ghost->translated(1,0),field.field)) ++ghost->position.row;
        }
    }
    for (const auto& cell:affected) {
        if (cell.row < -Runtime::kDisplayHiddenRows || cell.row>=field.visibleHeight ||
            cell.col<0 || cell.col>=field.width) continue;
        const bool marked=board_hint_cell(context,player,cell.row,cell.col);
        const bool activeCell=active && covers(*active,*p.paintedActivePose,cell.row,cell.col);
        const bool ghostCell=active && ghost && covers(*active,*ghost,cell.row,cell.col);
        if (activeCell || ghostCell) {
            move_to(context,player,cell.row,cell.col);
            const auto colors=activeCell ? cell_colors(active->color_id(),context.config.display.obscuredColors,p.paintedActiveDimmed) :
                ui_colors(ColorRole::Ghost,context.config.display.obscuredColors);
            if (marked) draw_spawn_square(context.config.display.squareStyle,colors);
            else {
                set_colors(colors);
                draw_square(context.config.display.squareStyle);
            }
        } else {
            draw_board_cell(context,player,cell.row,cell.col,field.field.cell(cell.row,cell.col));
        }
    }
}
} // namespace Render
