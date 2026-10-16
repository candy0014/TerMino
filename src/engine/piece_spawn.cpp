#include "tetr/engine/piece_spawn.h"

#include "tetr/network/multiplayer_channel.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/input/services.h"
#include "tetr/engine/player_input.h"
#include "tetr/engine/input_precharge.h"

#include <algorithm>

namespace Engine {

MovementState make_spawn_movement_state(int width, Piece::Type type) {
    MovementState state;
    state.pose = Piece::spawn_pose(width,type);
    return state;
}

bool apply_initial_movement(Runtime::GameContext& context, MovementInputState& input,
    MovementState& state, const MovementController::CanPlace& canPlace) {
    if(context.config.mode.replayMode || Runtime::autoplay_enabled(context.config.autoplay.mode)) return false;
    auto& services=context.input_services();
    precharge_movement_input(context,input);
    if (!context.config.handling.preMove) return false;
    const auto now=context.real_now();
    const auto& bindings=services.bindings;
    const auto moves=collect_action_keys(bindings,{Input::Action::MoveLeft,Input::Action::MoveRight});
    const int key=input.mostRecentActive(moves);
    if(!context.config.movement.allows_horizontal(Input::horizontal_delta(bindings.action_for(key)))) return false;
    if (!input.horizontalCharged(key,now,context.config.handling.das)) return false;
    const auto candidate=state.pose.translated(0,Input::horizontal_delta(bindings.action_for(key)));
    if (!canPlace(candidate)) return false;
    state.pose=candidate;
    ++state.inputActions;
    state.lastRotation=Runtime::RotationRecord::none();
    input.consumeInitialShift(key,now);
    const auto gravity=Runtime::current_gravity_interval(context);
    auto dropMode=gravity.is_instant() ? HorizontalDropMode::Gravity : HorizontalDropMode::None;
    if (!context.config.movement.disableSoftDrop &&
        soft_drop_interval(gravity,context.config.handling.softDropFactor,context.config.handling.softDropFixed)==Runtime::Duration{}) {
        for (const auto& [softKey,action]:services.bindings.entries()) {
            if (action==Input::Action::SoftDrop && services.key_down(softKey) && input.phase(softKey)!=KeyPhase::AwaitRelease) {
                dropMode=HorizontalDropMode::SoftDrop;break;
            }
        }
    }
    if (dropMode!=HorizontalDropMode::None) {
        MovementController movement(state,canPlace,[](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) {return false;});
        context.session.player.gameplay.scoring.score+=movement.dropToFloor(
            dropMode==HorizontalDropMode::SoftDrop ? 1 : 0,true,now).scoreDelta;
    }
    return true;
}

bool handle_spawn_collision(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    MovementState& state) {
    // Keep the pop-up for a Clutch-triggered turn visible through Hold.  It is
    // cleared only when the next independently spawned turn begins.
    if (!player.clutch.currentTurnLifted) {
        Render::print_clutch(context, player, 0);
    }
    const auto& cells = piece.cells(state.pose.rotation);
    int minimumColumn=cells.front()[1], maximumColumn=minimumColumn;
    for (const auto& offset:cells) {
        minimumColumn=std::min(minimumColumn,offset[1]);
        maximumColumn=std::max(maximumColumn,offset[1]);
    }
    if (Runtime::enabled(context.config.quickPlay.snowball) &&
        maximumColumn-minimumColumn+1<=field.width()) {
        // Event shapes need not share the tetromino's spawn origin. Adjust
        // the column only when the unrotated shape can fit inside the walls.
        state.pose=Piece::fit_spawn_inside_walls(state.pose,cells,field.width());
    }
    const bool playable= !Runtime::enabled(context.config.quickPlay.snowball) ||
        player.gameplay.playfield.activeHeight>0;
    if (playable && piece.can_place(state.pose, field)) return false;

    const bool insideWalls=state.pose.position.col+minimumColumn>=0 &&
        state.pose.position.col+maximumColumn<field.width();
    if (playable && player.clutch.previousPieceCleared && insideWalls) {
        int minimumCellOffset = cells.front()[0];
        for (const auto& offset : cells) {
            minimumCellOffset = std::min(minimumCellOffset, offset[0]);
        }
        const int minimumOriginRow =
            -Runtime::kHiddenRowCapacity - minimumCellOffset;
        Piece::Pose lifted = state.pose;
        while (--lifted.position.row >= minimumOriginRow) {
            if (!piece.can_place(lifted, field)) continue;
            state.pose = lifted;
            player.clutch.currentTurnLifted = true;
            ++player.clutch.consecutive;
            Render::print_clutch(context, player, player.clutch.consecutive);
            return false;
        }
    }

    if (context.config.field.invisible) {
        Render::OutputGuard output(context);
        for (int row = context.config.field.height - 1;
             row >= -Runtime::kHiddenRowCapacity;
             --row) {
            for (int column = 0; column < context.config.field.width; ++column) {
                const Runtime::BoardCell cell = field.cell(row, column);
                if (cell.empty_cell() || row < -Runtime::kDisplayHiddenRows) continue;
                Render::move_to(context, player, row, column);
                Render::set_board_cell_color(cell,context.config.display.obscuredColors);
                if (Render::spawn_marker_cell(context,player,row,column))
                    Render::draw_spawn_square(context.config.display.squareStyle,
                        Render::board_cell_colors(cell,context.config.display.obscuredColors));
                else Render::draw_square(context.config.display.squareStyle);
            }
        }
    }

    if (!context.config.mode.replayMode &&
        context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) {
        Network::send_round_signal(context, Network::OutgoingRoundSignal::LocalLoss);
    }
    return true;
}

void record_locked_piece_for_clutch(
    Runtime::PlayerSessionState& player,
    int clearedLines) noexcept {
    const bool cleared = clearedLines > 0;
    if (!cleared || !player.clutch.currentTurnLifted) {
        player.clutch.consecutive = 0;
    }
    player.clutch.previousPieceCleared = cleared;
    player.clutch.currentTurnLifted = false;
}

} // namespace Engine
