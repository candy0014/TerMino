#include "tetr/engine/live_piece_periodic_events.h"

#include "tetr/engine/garbage_service.h"
#include "piece_replay.h"
#include "garbage_rendering.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/rules/game_completion.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/recorder.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/player_session_state.h"

#include <iostream>

namespace Engine {

std::optional<Rules::PlayResult> process_live_piece_periodic_events(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    MovementState& movementState,
    bool deferCheeseTimer,
    const std::function<Rules::PlayResult()>& pumpFrame,
    bool pieceVisible, bool* fieldChanged) {
    if (fieldChanged) *fieldChanged=false;
    if (Runtime::uses_garbage_buffer(context.config.garbage.mode)) {
        GarbageService::update_buffer(context, player);
    }

    const Runtime::TimePoint now = context.now();
    if (Rules::time_goal_reached(context.config,Runtime::elapsed_game_time(context.session, now))) {
        Render::OutputGuard output(context);
        record_piece_pose(context, Replay::OperationKind::PutErase, movementState.pose);
        Render::draw_piece(
            context, piece, player, movementState.pose, field, Render::PieceDrawMode::Erase);
        Render::set_ui_color(
            context.config.display.obscuredColors ? Render::ColorRole::Ghost : Render::ColorRole::Text,
            context.config.display.obscuredColors);
        Render::print_message(context,player);
        return Rules::PlayResult::Locked;
    }

    const int activeHeightBefore=player.gameplay.playfield.activeHeight;
    if (periodic_event_due(context.real_now(), player.timing.framePoll, Network::multiplayer_frame_poll_interval(context))) {
        const auto paintBefore=player.presentation.activePiecePaintRevision;
        const Rules::PlayResult signal = pumpFrame();
        if (signal != Rules::PlayResult::ContinueFrame) return signal;
        // The frame may paint a piece that instant gravity initially deferred.
        // Include that overlay in the before-image of a following garbage rise.
        if (paintBefore!=player.presentation.activePiecePaintRevision)
            pieceVisible=player.presentation.paintedActivePose.has_value();
    }

    if (const auto signal=apply_immediate_garbage_to_live_piece(
            context,field,piece,player,movementState,pieceVisible,fieldChanged)) return signal;
    if (fieldChanged && *fieldChanged && Network::hybrid_board_sync_enabled(context))
        Network::send_board_state_frame(context,player,Network::ActivePieceState::active(movementState.pose),std::nullopt,true);
    if (fieldChanged && activeHeightBefore!=player.gameplay.playfield.activeHeight) *fieldChanged=true;

    if (!context.uses_piece_time() && context.config.garbage.mode == Runtime::GarbageMode::QuickPlay &&
        periodic_event_due(now, player.timing.qpGravity, Runtime::Duration::from_seconds(1.0))) {
        Runtime::set_current_gravity_interval(
            context,
            advance_qp_gravity_speed(Runtime::current_gravity_interval(context), true));
        player.timing.qpGravity = now;
    }

    if (context.config.garbage.mode == Runtime::GarbageMode::Multiplayer &&
        periodic_event_due(context.real_now(), player.timing.multiplayerSync, Runtime::Duration::from_seconds(0.1))) {
        Network::send_board_state_frame(
            context, player, Network::ActivePieceState::active(movementState.pose));
        player.timing.multiplayerSync = context.real_now();
    }

    // A bot's Hold continuation still uses the field from its earlier choice.
    // Keep timed garbage pending until that piece has locked.
    if (!context.uses_piece_time() && !deferCheeseTimer &&
        context.config.garbage.mode == Runtime::GarbageMode::CheeseTimer &&
        periodic_event_due(now, player.timing.timedGarbage, context.config.garbage.generationInterval)) {
        Render::OutputGuard output(context);
        const auto before=GarbageRendering::capture_field(context,player,field,
            pieceVisible ? &piece : nullptr,movementState.pose);
        record_piece_pose(context, Replay::OperationKind::PutErase, movementState.pose);
        GarbageService::add_garbage(context,player,field,1,false,Runtime::BoardCell::garbage(),false);
        if (!piece.can_place(movementState.pose, field)) {
            --movementState.pose.position.row;
        }
        record_piece_pose(context, Replay::OperationKind::PutDraw, movementState.pose);
        GarbageRendering::render_field_changes(context,player,field,before,&piece,movementState.pose);
        Render::flush();
        player.timing.timedGarbage = now;
    }

    return std::nullopt;
}

void apply_due_cheese_timer_after_autoplay_lock(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player) {
    if (context.config.mode.replayMode || context.uses_piece_time() ||
        context.config.garbage.mode != Runtime::GarbageMode::CheeseTimer ||
        !Runtime::autoplay_enabled(context.config.autoplay.mode)) return;

    const Runtime::TimePoint now = context.now();
    if (!periodic_event_due(now, player.timing.timedGarbage,
                            context.config.garbage.generationInterval)) return;
    GarbageService::add_garbage(context, player, field, 1);
    player.timing.timedGarbage = now;
}

} // namespace Engine
