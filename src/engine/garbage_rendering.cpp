#include "garbage_rendering.h"
#include "tetr/presentation/context_view.h"

#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>

namespace Engine::GarbageRendering {
namespace {

int color_id(const Runtime::GameContext& context, Runtime::BoardCell value) {
    if (value.empty_cell()) return -1;
    if (value.is_timed_number() || value.is_timed_release()) return -2;
    return context.config.display.obscuredColors ? 0 : value.color_id();
}

Render::TerminalColors colors_for(const Runtime::GameContext& context,int color,bool dimmed) {
    if (color==-1) return Render::ui_colors(Render::ColorRole::Ghost,context.config.display.obscuredColors);
    if (color==-2) return Render::ui_colors(Render::ColorRole::NumberedCell,context.config.display.obscuredColors);
    return Render::cell_colors(color,context.config.display.obscuredColors,dimmed);
}
void set_color(const Runtime::GameContext& context,int color,bool dimmed) {
    Render::set_colors(colors_for(context,color,dimmed));
}

} // namespace

FieldImage capture_field(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, const Runtime::Playfield& field,
    const Piece::Definition* piece, std::optional<Piece::Pose> pose) {
    FieldImage image;
    const auto view=Presentation::capture_board_view(context.config,player,field,Presentation::board_elapsed(context,player));
    image.width=view.width;
    image.bottom=view.bottom;
    image.cells.reserve((image.bottom+Runtime::kDisplayHiddenRows)*image.width);
    for (int row=-Runtime::kDisplayHiddenRows;row<image.bottom;++row)
        for (int column=0;column<image.width;++column) {
            const auto& cell=view.at(row,column);
            const auto value=cell.value;
            const auto appearance=cell.appearance;
            image.cells.push_back({color_id(context,value),value.timed_countdown(),
                appearance!=Render::BoardCellAppearance::Empty,appearance==Render::BoardCellAppearance::Dimmed});
        }
    const auto overlay=[&](Piece::Pose at,int color,bool dimmed=false) {
        for (const auto& offset:piece->cells(at.rotation)) {
            const int row=at.position.row+offset[0], column=at.position.col+offset[1];
            if (row < -Runtime::kDisplayHiddenRows || row>=image.bottom || column<0 || column>=image.width) continue;
            image.cells[(row+Runtime::kDisplayHiddenRows)*image.width+column]={color,0,true,dimmed};
        }
    };
    const auto activeMode=Render::active_piece_draw_mode(context,player);
    if (piece && pose && activeMode!=Render::PieceDrawMode::Erase) {
        if (context.config.field.ghost) {
            auto ghost=*pose;
            while (piece->can_place(ghost.translated(1,0),field)) ++ghost.position.row;
            overlay(ghost,-1);
        }
        overlay(*pose,context.config.display.obscuredColors ? 0 : piece->color_id(),activeMode==Render::PieceDrawMode::Dimmed);
    }
    return image;
}

void render_field_changes(const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player, const Runtime::Playfield& field,
    const FieldImage& before, const Piece::Definition* piece, std::optional<Piece::Pose> pose) {
    const auto after=capture_field(context,player,field,piece,pose);
    assert(before.width==after.width && before.bottom==after.bottom);
    Render::OutputGuard output(context);
    int color=std::numeric_limits<int>::min();
    bool dimmed=false;
    int previousRow=std::numeric_limits<int>::min();
    int nextColumn=-1;
    for (std::size_t index=0;index<after.cells.size();++index) {
        if (before.cells[index]==after.cells[index]) continue;
        const int row=static_cast<int>(index)/after.width-Runtime::kDisplayHiddenRows;
        const int column=static_cast<int>(index)%after.width;
        const auto& cell=after.cells[index];
        const bool nextMarked=Render::spawn_marker_cell(context,player,row,column);
        if (row!=previousRow || column!=nextColumn) Render::move_to(context,player,row,column);
        previousRow=row;
        // Circled numbers can occupy one column in the spaced style. Clear
        // their old square first, then position the next cell explicitly.
        if (cell.number) {
            Render::set_ui_color(Render::ColorRole::Ghost,context.config.display.obscuredColors);
            Render::draw_square(context.config.display.squareStyle,Render::SquareFill::Empty);
            Render::move_to(context,player,row,column);
            set_color(context,cell.color,cell.dimmed);
            if (nextMarked) Render::draw_spawn_number(context.config.display.squareStyle,cell.number,
                colors_for(context,cell.color,cell.dimmed));
            else Render::draw_number(context.config.display.squareStyle,cell.number);
            nextColumn=-1;
            color=cell.color;
            dimmed=cell.dimmed;
            continue;
        }
        // Blanks need only the normal background; keep the current foreground
        // across holes instead of switching colors twice for every hole.
        const int nextColor=cell.filled ? cell.color :
            (color!=-2 && color!=std::numeric_limits<int>::min() ? color : -1);
        const bool nextDimmed=cell.filled ? cell.dimmed : dimmed;
        if (color!=nextColor || dimmed!=nextDimmed) {
            set_color(context,nextColor,nextDimmed); color=nextColor; dimmed=nextDimmed;
        }
        if (!cell.filled && nextMarked) {
            Render::draw_empty_board_cell(context,player,row,column);
            // Drawing the dot changes the foreground; invalidate its cache.
            color=std::numeric_limits<int>::min();
        } else if (cell.filled && nextMarked) {
            Render::draw_spawn_square(context.config.display.squareStyle,colors_for(context,cell.color,cell.dimmed));
        } else {
            Render::draw_square(context.config.display.squareStyle,
                cell.filled ? Render::SquareFill::Filled : Render::SquareFill::Empty);
        }
        nextColumn=column+1==after.width ? -1 : column+1;
    }
    if (piece && pose) {
        const auto mode=Render::active_piece_draw_mode(context,player);
        player.presentation.paintedActivePose=mode==Render::PieceDrawMode::Erase?std::optional<Piece::Pose>{}:pose;
        player.presentation.paintedActivePieceType=mode==Render::PieceDrawMode::Erase?std::optional<Piece::Type>{}:piece->type();
        player.presentation.paintedActiveDimmed=mode==Render::PieceDrawMode::Dimmed;
        ++player.presentation.activePiecePaintRevision;
        if (player.presentation.activePieceType==piece->type()) player.presentation.activePose=pose;
    }
}

} // namespace Engine::GarbageRendering
