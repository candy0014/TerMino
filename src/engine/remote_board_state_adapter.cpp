#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/network/hybrid_protocol.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"

namespace Engine {
using namespace Render;

bool apply_remote_piece_motion(Runtime::GameContext& context,Runtime::PlayerSessionState& player,
                              const Network::PieceMotionFrame& motion) {
    const auto piece=Piece::get_definition(motion.piece);
    Piece::Pose pose=motion.result;
    if (player.presentation.activePose && player.presentation.activePieceType==motion.piece) {
        // Replay the resolved translation/rotation locally. The absolute
        // result repairs a missing predecessor without replaying input history.
        auto computed=player.presentation.activePose->translated(motion.rows,motion.columns);
        computed.rotation=computed.rotation.rotated(motion.rotations);
        if (computed==motion.result) pose=computed;
    }
    if (!piece.can_place(pose,player.gameplay.playfield.field)) return false;
    const bool changed=player.presentation.activePose!=pose || player.presentation.activePieceType!=motion.piece;
    if (!changed) return true;
    OutputGuard output(context);
    if (player.presentation.activePose && player.presentation.activePieceType)
        draw_board_piece(context,player,*player.presentation.activePieceType,*player.presentation.activePose,PieceDrawMode::Erase);
    player.presentation.activePose=pose; player.presentation.activePieceType=motion.piece;
    draw_board_piece(context,player,motion.piece,pose);
    return true;
}

void apply_remote_board_state(Runtime::GameContext& context,Runtime::PlayerSessionState& player,
                              const Network::BoardStateSnapshot& snapshot) {
    OutputGuard output(context);
    auto& field=player.gameplay.playfield;
    auto& presentation=player.presentation;
    bool boardChanged=false;
    for (int row=field.visibleHeight-1;row>=-Runtime::kDisplayHiddenRows && !boardChanged;--row)
        for (int col=0;col<field.width;++col)
            if (field.field.cell(row,col)!=snapshot.field.cell(row,col)) {boardChanged=true; break;}
    const bool activeChanged=snapshot.placement || boardChanged || presentation.activePose!=snapshot.activePose ||
        presentation.activePieceType!=snapshot.currentPiece;
    if (activeChanged && presentation.activePose && presentation.activePieceType)
        draw_board_piece(context,player,*presentation.activePieceType,*presentation.activePose,PieceDrawMode::Erase);
    if (snapshot.placement) {presentation.activePose.reset(); presentation.activePieceType.reset();}

    player.gameplay.scoring.wins=snapshot.score.wins;
    player.gameplay.scoring.placedBlocks=snapshot.score.placedBlocks;
    player.gameplay.scoring.totalAttack=snapshot.score.totalAttack;
    auto& hold=player.gameplay.hold;
    const bool available=snapshot.holdAvailable.value_or(true);
    if (hold.piece!=snapshot.hold || hold.remoteAvailable!=available) {
        draw_board_hold(context,player,PieceDrawMode::Erase);
        hold.piece=snapshot.hold; hold.remoteAvailable=available; draw_board_hold(context,player);
    }
    for (std::size_t i=0;i<snapshot.garbage.size();++i) {
        move_to(context,player,field.visibleHeight-static_cast<int>(i)-1,0,-3);
        switch (snapshot.garbage[i]) {
            case Network::RemoteGarbageCell::Empty: draw_square(context.config.display.squareStyle,SquareFill::Empty); break;
            case Network::RemoteGarbageCell::Incoming:
                set_ui_color(ColorRole::IncomingGarbage,context.config.display.obscuredColors); draw_square(context.config.display.squareStyle); break;
            case Network::RemoteGarbageCell::EarlyBuffer:
                set_ui_color(ColorRole::Accent,context.config.display.obscuredColors); draw_square(context.config.display.squareStyle); break;
            case Network::RemoteGarbageCell::Ready:
                set_ui_color(ColorRole::Danger,context.config.display.obscuredColors); draw_square(context.config.display.squareStyle); break;
        }
    }
    // A placement checkpoint repairs the complete field as well as its HUD.
    // This also recovers after losing the following spawn snapshot.
    for (int row=field.visibleHeight-1;row>=-Runtime::kDisplayHiddenRows;--row) {
        for (int col=0;col<field.width;++col) {
            const auto cell=snapshot.field.cell(row,col);
            const bool changed=field.field.cell(row,col)!=cell;
            field.field.set_cell(row,col,cell);
            if (!changed) continue;
            move_to(context,player,row,col);
            set_board_cell_color(cell,context.config.display.obscuredColors);
            const bool marked=spawn_marker_cell(context,player,row,col);
            const auto colors=board_cell_colors(cell,context.config.display.obscuredColors);
            if (cell.is_timed_number()) {
                if (marked) draw_spawn_number(context.config.display.squareStyle,cell.timed_countdown(),colors);
                else draw_number(context.config.display.squareStyle,cell.timed_countdown());
            } else if (cell.occupied()) {
                if (marked) draw_spawn_square(context.config.display.squareStyle,colors);
                else draw_square(context.config.display.squareStyle);
            } else draw_empty_board_cell(context,player,row,col);
        }
    }
    if (!snapshot.placement && activeChanged) {
        presentation.activePose=snapshot.activePose; presentation.activePieceType=snapshot.currentPiece;
        draw_board_piece(context,player,snapshot.currentPiece,snapshot.activePose);
    }
    if (player.pieces.nextPreview!=snapshot.nextPreview) {
        for (int i=0;i<field.nextCount;++i) draw_board_next(context,player,i,PieceDrawMode::Erase);
        player.pieces.nextPreview=snapshot.nextPreview;
        for (int i=0;i<field.nextCount;++i) draw_board_next(context,player,i);
    }
    presentation.nextSpawnType=snapshot.nextSpawnType ? snapshot.nextSpawnType :
        snapshot.nextPreview.empty() ? std::optional<Piece::Type>{} : snapshot.nextPreview.front();
    presentation.nextSpawnPose=snapshot.nextSpawnPose;
    refresh_spawn_marker(context,player);
    if (snapshot.placement) {
        const auto& placement=*snapshot.placement;
        print_spin(context,player,Piece::Type::none(),false);
        if (placement.spin) print_spin(context,player,placement.piece,placement.mini);
        print_clear(context,player,placement.clearedLines);
        print_b2b(context,player,placement.b2b); print_combo(context,player,placement.combo);
        print_all_clear(context,player,placement.perfectClear,placement.halfClear);
    }
    print_message(context,player); print_score(context,player);
}
} // namespace Engine
