#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/network/hybrid_protocol.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/rules/spike.h"
#include "tetr/rules/lock_analysis.h"

namespace Engine {
using namespace Render;
namespace {
void capture_remote_placement(Runtime::PlayerSessionState& player,const Network::BoardStateSnapshot& snapshot) {
    if(!snapshot.placement)return;
    auto& feedback=player.presentation.placement;
    // Duplicate repair checkpoints must retain the pre-lock geometry. The
    // following spawn/board repair already contains the field after the clear.
    if(feedback.sequence==snapshot.score.placedBlocks && feedback.sequence>0)return;
    const auto& event=*snapshot.placement;
    feedback={};feedback.sequence=snapshot.score.placedBlocks;feedback.clearedLines=event.clearedLines;
    const auto& p=player.presentation;
    if(snapshot.score.placedBlocks!=player.gameplay.scoring.placedBlocks+1 ||
        !event.piece.valid() || p.activePieceType!=event.piece || !p.activePose)return;
    const auto& field=player.gameplay.playfield.field;
    if(field.width()!=snapshot.field.width() || field.height()!=snapshot.field.height())return;
    const auto piece=Piece::get_definition(event.piece);
    auto landing=*p.activePose;
    if(!piece.can_place(landing,field))return;
    while(piece.can_place(landing.translated(1,0),field))++landing.position.row;
    feedback.type=event.piece;feedback.pose=landing;feedback.dropFromRow=p.activePose->position.row;
    // Reuse shared lock analysis for the observed pose; never mutate the field
    // or apply a second gameplay placement. The remote count is authoritative.
    const auto cells=piece.cells(landing.rotation);
    const auto result=Rules::analyze_locked_field(field.width(),-Runtime::kDisplayHiddenRows,field.height(),field.height(),field.height(),
        [&](int row,int col) {
            for(const auto& cell:cells)if(row==landing.position.row+cell[0] && col==landing.position.col+cell[1])
                return Runtime::BoardCell::piece(piece.color_id());
            return field.cell(row,col);
        });
    if(result.clearedLines==event.clearedLines)feedback.clearedRows=result.clearedRows;
}
void update_clock(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,
    const std::optional<Network::RemoteClockSample>& clock) {
    if(!clock) return;
    player.timing.remoteGameTime=Runtime::Duration::from_seconds(clock->elapsedSeconds);
    player.timing.remoteTimeRate=clock->rate;player.timing.remoteTimeUpdated=context.services.clock.now();
    if(player.timing.completedGameTime && clock->rate==0)
        player.timing.completedGameTime=player.timing.remoteGameTime;
}
}

void apply_remote_clock_sample(Runtime::GameContext& context,Runtime::PlayerSessionState& player,const Network::RemoteClockSample& sample) {
    update_clock(context,player,sample);
}

bool apply_remote_piece_motion(Runtime::GameContext& context,Runtime::PlayerSessionState& player,
                              const Network::PieceMotionFrame& motion,bool render) {
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
    update_clock(context,player,motion.clock);
    const bool changed=player.presentation.activePose!=pose || player.presentation.activePieceType!=motion.piece;
    if (!changed) return true;
    if(!render) {player.presentation.activePose=pose;player.presentation.activePieceType=motion.piece;return true;}
    OutputGuard output(context);
    if (player.presentation.activePose && player.presentation.activePieceType)
        draw_board_piece(context,player,*player.presentation.activePieceType,*player.presentation.activePose,PieceDrawMode::Erase);
    player.presentation.activePose=pose; player.presentation.activePieceType=motion.piece;
    draw_board_piece(context,player,motion.piece,pose);
    return true;
}

void apply_remote_board_state(Runtime::GameContext& context,Runtime::PlayerSessionState& player,
                              const Network::BoardStateSnapshot& snapshot,bool render) {
    capture_remote_placement(player,snapshot);
    if(context.session.roomScene.enabled) {
        auto& p=player.presentation;const auto& score=player.gameplay.scoring;
        if(snapshot.score.totalAttack<score.totalAttack || snapshot.score.placedBlocks<score.placedBlocks)p.spike={};
        const int attack=snapshot.score.totalAttack-score.totalAttack;
        if(attack>0 && p.remoteAttackBaselineKnown)
            Rules::add_spike_attack(p.spike,attack,context.now());
        p.remoteAttackBaselineKnown=true;
    }
    update_clock(context,player,snapshot.clock);
    const bool activeValid=!snapshot.placement && snapshot.currentPiece.valid() &&
        Piece::get_definition(snapshot.currentPiece).can_place(snapshot.activePose,snapshot.field);
    if(!render) {
        player.gameplay.playfield.field=snapshot.field;
        player.gameplay.playfield.width=snapshot.field.width();player.gameplay.playfield.visibleHeight=snapshot.field.height();
        player.gameplay.playfield.activeHeight=snapshot.field.height();
        player.gameplay.scoring.wins=snapshot.score.wins;player.gameplay.scoring.placedBlocks=snapshot.score.placedBlocks;
        player.gameplay.scoring.totalAttack=snapshot.score.totalAttack;
        player.gameplay.hold.piece=snapshot.hold;player.gameplay.hold.remoteAvailable=snapshot.holdAvailable.value_or(true);
        player.pieces.nextPreview=snapshot.nextPreview;
        player.presentation.activePose=activeValid?std::optional<Piece::Pose>{snapshot.activePose}:std::nullopt;
        player.presentation.activePieceType=activeValid?std::optional<Piece::Type>{snapshot.currentPiece}:std::nullopt;
        if(!player.presentation.activePieceType) player.presentation.activePose.reset();
        player.presentation.nextSpawnType=snapshot.nextSpawnType;player.presentation.nextSpawnPose=snapshot.nextSpawnPose;
        if(snapshot.placement) {
            const auto& placement=*snapshot.placement;
            player.gameplay.scoring.combo=placement.combo;player.gameplay.scoring.b2b=placement.b2b;
            auto& p=player.presentation;++p.remotePlacementRevision;
            p.remoteSpinPiece=placement.spin?placement.piece:Piece::Type::none();p.remoteMini=placement.mini;
            p.remoteClearLines=placement.clearedLines;p.remotePerfectClear=placement.perfectClear;p.remoteHalfClear=placement.halfClear;
        }
        player.presentation.garbageBufferDisplay.clear();
        for(const auto cell:snapshot.garbage) player.presentation.garbageBufferDisplay.push_back(static_cast<int>(cell));
        return;
    }
    OutputGuard output(context);
    const bool visibilityChanged=synchronize_field_visibility(context,player);
    auto& field=player.gameplay.playfield;
    auto& presentation=player.presentation;
    bool boardChanged=visibilityChanged;
    for (int row=field.visibleHeight-1;row>=-Runtime::kDisplayHiddenRows && !boardChanged;--row)
        for (int col=0;col<field.width;++col)
            if (field.field.cell(row,col)!=snapshot.field.cell(row,col) ||
                field.field.cell_lock_time(row,col)!=snapshot.field.cell_lock_time(row,col)) {boardChanged=true; break;}
    const bool activeChanged=snapshot.placement || boardChanged || presentation.activePose!=snapshot.activePose ||
        presentation.activePieceType!=snapshot.currentPiece;
    if (activeChanged && presentation.activePose && presentation.activePieceType)
        draw_board_piece(context,player,*presentation.activePieceType,*presentation.activePose,PieceDrawMode::Erase);
    if (!activeValid) {presentation.activePose.reset(); presentation.activePieceType.reset();}

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
            const auto lockedAt=snapshot.field.cell_lock_time(row,col);
            const bool changed=visibilityChanged || field.field.cell(row,col)!=cell || field.field.cell_lock_time(row,col)!=lockedAt;
            field.field.set_cell(row,col,cell);
            field.field.mark_cell_lock_time(row,col,lockedAt);
            if (!changed) continue;
            draw_board_cell(context,player,row,col,cell);
        }
    }
    if (activeValid && activeChanged) {
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
