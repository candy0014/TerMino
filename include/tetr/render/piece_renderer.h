#ifndef TETR_RENDER_PIECE_RENDERER_H
#define TETR_RENDER_PIECE_RENDERER_H

#include "tetr/runtime/piece.h"
#include <vector>

namespace Runtime { class Playfield; class GameContext; struct PlayerSessionState; }

namespace Render {
bool active_piece_visible(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player);
enum class PieceDrawMode { Erase, Normal, Ghost, Dimmed };
PieceDrawMode active_piece_draw_mode(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player);
PieceDrawMode hold_draw_mode(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, int additionalUses=0);

void draw_block(const Runtime::GameContext& context,const Piece::Definition& piece,Runtime::PlayerSessionState& player,Piece::Pose pose,PieceDrawMode mode=PieceDrawMode::Normal);
void draw_piece(const Runtime::GameContext& context,Piece::Definition& piece,Runtime::PlayerSessionState& player,Piece::Pose pose,Runtime::Playfield& field,PieceDrawMode mode=PieceDrawMode::Normal);
// Update only cells whose occupancy/color changes, and flush once.
void draw_piece_transition(const Runtime::GameContext& context, const Piece::Definition& piece,
    Runtime::PlayerSessionState& player, Piece::Pose before, Piece::Pose after,
    const Runtime::Playfield& field);
void draw_hold(const Runtime::GameContext& context,const Piece::Definition& piece,Runtime::PlayerSessionState& player,PieceDrawMode mode=PieceDrawMode::Normal);
void draw_next(const Runtime::GameContext& context,const Piece::Definition& piece,Runtime::PlayerSessionState& player,int previewIndex,PieceDrawMode mode=PieceDrawMode::Normal);
// Compare terminal columns, including odd/even preview centering. Unchanged
// pixels stay painted; one flush presents the entire NEXT update.
void draw_next_preview_transition(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,
                                  const std::vector<Piece::Type>& previous);
void draw_board_piece(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,Piece::Type pieceType,Piece::Pose pose,PieceDrawMode mode=PieceDrawMode::Normal);
void draw_board_hold(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,PieceDrawMode mode=PieceDrawMode::Normal);
void draw_board_next(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,int previewIndex,PieceDrawMode mode=PieceDrawMode::Normal);
} // namespace Render
#endif
