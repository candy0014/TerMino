#include "tetr/render/piece_renderer.h"

#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include <algorithm>
#include <vector>
#include <map>
#include <iostream>

namespace Render {
bool active_piece_visible(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player) {
    const auto rule=context.config.challengeRules;
    if(rule==Runtime::ChallengeRules::Blind || rule==Runtime::ChallengeRules::Audio)return false;
    return rule!=Runtime::ChallengeRules::Instinct || board_elapsed_seconds(context,player)<player.presentation.activePieceBorn.seconds()+.2;
}
PieceDrawMode active_piece_draw_mode(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player) {
    if(active_piece_visible(context,player))return PieceDrawMode::Normal;
    return context.config.mode.replayMode?PieceDrawMode::Dimmed:PieceDrawMode::Erase;
}
PieceDrawMode hold_draw_mode(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, int additionalUses) {
    const auto& hold=player.gameplay.hold;
    const bool available=hold.remoteAvailable.value_or(
        Runtime::hold_available(context.config.field,hold.used+additionalUses));
    return available ? PieceDrawMode::Normal : PieceDrawMode::Ghost;
}

void draw_block(const Runtime::GameContext& context,const Piece::Definition& piece,Runtime::PlayerSessionState& player,Piece::Pose pose,PieceDrawMode mode){
    OutputGuard output(context);
    const bool colored=mode==PieceDrawMode::Normal || mode==PieceDrawMode::Dimmed;
    if(colored) set_cell_color(piece.color_id(),context.config.display.obscuredColors,mode==PieceDrawMode::Dimmed);
    else set_ui_color(ColorRole::Ghost,context.config.display.obscuredColors);
    for(const auto& det:piece.cells(pose.rotation)){
        const int row=pose.position.row+det[0],column=pose.position.col+det[1];
        if(mode==PieceDrawMode::Erase)draw_board_cell(context,player,row,column,player.gameplay.playfield.field.cell(row,column));
        else {
            move_to(context,player,row,column);
            if(board_hint_cell(context,player,row,column))draw_spawn_square(context.config.display.squareStyle,
                colored?cell_colors(piece.color_id(),context.config.display.obscuredColors,mode==PieceDrawMode::Dimmed):ui_colors(ColorRole::Ghost,context.config.display.obscuredColors));
            else draw_square(context.config.display.squareStyle);
        }
    }
    move_to(context,player,context.config.field.height+2,0);
}

void draw_piece(const Runtime::GameContext& context,Piece::Definition& piece,Runtime::PlayerSessionState& player,Piece::Pose pose,Runtime::Playfield& field,PieceDrawMode mode){
    OutputGuard output(context);
    if(mode!=PieceDrawMode::Erase && !active_piece_visible(context,player))mode=active_piece_draw_mode(context,player);
    Piece::Pose ghost=pose;
    if(context.config.field.ghost)
        while(piece.can_place(ghost.translated(1,0),field)) ++ghost.position.row;
    if(context.config.field.ghost) draw_block(context,piece,player,ghost,
        mode==PieceDrawMode::Erase?PieceDrawMode::Erase:PieceDrawMode::Ghost);
    draw_block(context,piece,player,pose,mode);
    player.presentation.paintedActivePose=mode==PieceDrawMode::Erase
        ? std::optional<Piece::Pose>{} : pose;
    player.presentation.paintedActivePieceType=mode==PieceDrawMode::Erase
        ? std::optional<Piece::Type>{} : piece.type();
    player.presentation.paintedActiveDimmed=mode==PieceDrawMode::Dimmed;
    ++player.presentation.activePiecePaintRevision;
    if (mode!=PieceDrawMode::Erase && player.presentation.activePieceType==piece.type())
        player.presentation.activePose=pose;
    refresh_spawn_marker(context,player);
    flush();
}

void draw_piece_transition(const Runtime::GameContext& context, const Piece::Definition& piece,
    Runtime::PlayerSessionState& player, Piece::Pose before, Piece::Pose after,
    const Runtime::Playfield& field) {
    const auto activeMode=active_piece_draw_mode(context,player);
    if(activeMode==PieceDrawMode::Erase) {
        auto mutablePiece=piece;auto& mutableField=player.gameplay.playfield.field;
        if(player.presentation.paintedActivePose)draw_piece(context,mutablePiece,player,*player.presentation.paintedActivePose,mutableField,PieceDrawMode::Erase);
        return;
    }
    OutputGuard output(context);
    struct Cell { int row; int col; PieceDrawMode mode; };
    const auto find = [](auto& cells, int row, int col) {
        return std::find_if(cells.begin(), cells.end(), [=](const Cell& cell) {
            return cell.row == row && cell.col == col;
        });
    };
    const auto cellsAt = [&](Piece::Pose pose,PieceDrawMode active) {
        std::vector<Cell> cells;
        cells.reserve(piece.cells(pose.rotation).size() * 2);
        const auto append = [&](Piece::Pose p, PieceDrawMode mode) {
            for (const auto& offset : piece.cells(p.rotation)) {
                const int row = p.position.row + offset[0], col = p.position.col + offset[1];
                const auto it = find(cells, row, col);
                if (it == cells.end()) cells.push_back({row, col, mode});
                else it->mode = mode;
            }
        };
        if (context.config.field.ghost) {
            auto ghost = pose;
            while (piece.can_place(ghost.translated(1, 0), field)) ++ghost.position.row;
            append(ghost, PieceDrawMode::Ghost);
        }
        append(pose, active); // Active cells cover overlapping ghost cells.
        return cells;
    };
    const auto oldCells = cellsAt(before,player.presentation.paintedActiveDimmed?PieceDrawMode::Dimmed:PieceDrawMode::Normal);
    const auto newCells = cellsAt(after,activeMode);
    for (const auto& cell : oldCells) {
        if (find(newCells, cell.row, cell.col) != newCells.end()) continue;
        draw_board_cell(context,player,cell.row,cell.col,field.cell(cell.row,cell.col));
    }
    PieceDrawMode color = PieceDrawMode::Erase;
    for (const auto& cell : newCells) {
        const auto old = find(oldCells, cell.row, cell.col);
        if (old != oldCells.end() && old->mode == cell.mode) continue;
        if (color != cell.mode) {
            if (cell.mode == PieceDrawMode::Normal || cell.mode==PieceDrawMode::Dimmed) set_cell_color(piece.color_id(), context.config.display.obscuredColors,cell.mode==PieceDrawMode::Dimmed);
            else set_ui_color(ColorRole::Ghost, context.config.display.obscuredColors);
            color = cell.mode;
        }
        move_to(context, player, cell.row, cell.col);
        if(board_hint_cell(context,player,cell.row,cell.col))draw_spawn_square(context.config.display.squareStyle,
            cell.mode!=PieceDrawMode::Ghost?cell_colors(piece.color_id(),context.config.display.obscuredColors,cell.mode==PieceDrawMode::Dimmed):ui_colors(ColorRole::Ghost,context.config.display.obscuredColors));
        else draw_square(context.config.display.squareStyle);
    }
    move_to(context, player, context.config.field.height + 2, 0);
    player.presentation.paintedActivePose=after;
    player.presentation.paintedActivePieceType=piece.type();
    player.presentation.paintedActiveDimmed=activeMode==PieceDrawMode::Dimmed;
    ++player.presentation.activePiecePaintRevision;
    if (player.presentation.activePieceType==piece.type()) player.presentation.activePose=after;
    refresh_spawn_marker(context,player);
    flush();
}

void draw_hold(const Runtime::GameContext& context,const Piece::Definition& piece,Runtime::PlayerSessionState& player,PieceDrawMode mode){
    OutputGuard output(context);
    if(mode==PieceDrawMode::Normal) set_cell_color(piece.color_id(),context.config.display.obscuredColors);
    else set_ui_color(ColorRole::Ghost,context.config.display.obscuredColors);
    for(const auto& det:Piece::preview_cells(piece.type())){
        move_hold(context,player,det[0],det[1],piece.type());
        draw_square(context.config.display.squareStyle,mode==PieceDrawMode::Erase?SquareFill::Empty:SquareFill::Filled);
    }
    flush();
}

void draw_next(const Runtime::GameContext& context,const Piece::Definition& piece,Runtime::PlayerSessionState& player,int previewIndex,PieceDrawMode mode){
    OutputGuard output(context);
    if(player.gameplay.playfield.nextCount==0) return;
    set_cell_color(piece.color_id(),context.config.display.obscuredColors);
    for(const auto& det:Piece::preview_cells(piece.type())){
        move_next(context,player,previewIndex,det[0],det[1],piece.type());
        draw_square(context.config.display.squareStyle,mode==PieceDrawMode::Erase?SquareFill::Empty:SquareFill::Filled);
    }
    flush();
}

void draw_next_preview_transition(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,
    const std::vector<Piece::Type>& previous) {
    using Pixel=std::pair<int,int>;
    const auto image=[&](const auto& preview) {
        std::map<Pixel,int> pixels;
        const auto& p=player.presentation;
        const auto& board=player.gameplay.playfield;
        for (std::size_t index=0;index<preview.size();++index) {
            if (!preview[index].valid()) continue;
            const auto metadata=Piece::metadata(preview[index]);
            for (const auto& cell:Piece::preview_cells(preview[index])) {
                const int row=cell[0]+(static_cast<int>(index)+1)*(p.blockMaxHeight+1)-
                    (p.blockMaxHeight-2+metadata.displayRowOffset)/2+p.topMargin;
                const int column=(cell[1]+board.width+p.blockMaxWidth/2)*2+p.margin+
                    p.blockMaxWidth%2+metadata.displayWidth%2;
                pixels[{row,column}]=metadata.colorId;
                if (context.config.display.squareStyle==0) pixels[{row,column+1}]=metadata.colorId;
            }
        }
        return pixels;
    };
    const auto before=image(previous),after=image(player.pieces.nextPreview);
    if (before==after) return;
    auto changes=after;
    for (const auto& [position,color]:before) if (!after.count(position)) changes[position]=-1;
    OutputGuard output(context);
    int lastRow=-1,nextColumn=-1,lastColor=-2;
    for (const auto& [position,color]:changes) {
        const auto old=before.find(position);
        if (old!=before.end() && old->second==color) continue;
        if (position.first!=lastRow || position.second!=nextColumn) move_cursor(position.first,position.second);
        if (color>=0 && color!=lastColor) {
            set_cell_color(color,context.config.display.obscuredColors); lastColor=color;
        }
        if (color<0 && lastColor==-2) { set_ui_color(ColorRole::Ghost,context.config.display.obscuredColors); lastColor=-1; }
        std::cout<<(color<0 ? " " : "█");
        lastRow=position.first;nextColumn=position.second+1;
    }
    flush();
}

void draw_board_piece(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,Piece::Type pieceType,Piece::Pose pose,PieceDrawMode mode){
    OutputGuard output(context);
    auto piece=Piece::get_definition(pieceType);
    draw_piece(context,piece,player,pose,player.gameplay.playfield.field,mode);
}

void draw_board_hold(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,PieceDrawMode mode){
    OutputGuard output(context);
    Piece::Type type=player.gameplay.hold.piece;
    if(!type.valid()) return;
    if(context.config.quickPlay.fool == Runtime::FoolMode::ReverseAprilFools2026) type=Piece::fool_display_type(type);
    const auto piece=Piece::get_definition(type);
    draw_hold(context,piece,player,mode==PieceDrawMode::Normal ? hold_draw_mode(context,player) : mode);
}

void draw_board_next(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,int previewIndex,PieceDrawMode mode){
    OutputGuard output(context);
    if(previewIndex<0 || previewIndex>=player.gameplay.playfield.nextCount ||
       static_cast<std::size_t>(previewIndex)>=player.pieces.nextPreview.size()) return;
    const Piece::Type type=player.pieces.nextPreview[previewIndex];
    if(!type.valid() || static_cast<std::size_t>(type.index())>=Piece::catalog().size()) return;
    const auto piece=Piece::get_definition(type);
    draw_next(context,piece,player,previewIndex,mode);
}

} // namespace Render
