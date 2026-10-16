#include "tetr/input/services.h"
#include "live_piece_controls.h"

#include "piece_replay.h"
#include "tetr/engine/player_input.h"
#include "tetr/render/audio_cues.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/undo.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/replay/recorder.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"

namespace Engine {
namespace {

void perform_live_hold(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    Piece::Pose drawnPose,
    Piece::Pose currentPose) {
    auto& hold = player.gameplay.hold;
    record_piece_pose(context, Replay::OperationKind::PutErase, currentPose);
    Render::draw_piece(context, piece, player, player.presentation.paintedActivePose.value_or(drawnPose),
        field, Render::PieceDrawMode::Erase);
    Replay::add_replay(context, Replay::OperationKind::Hold);
    if (hold.piece.valid()) {
        Render::draw_board_hold(context, player, Render::PieceDrawMode::Erase);
    }
    if (context.config.quickPlay.fool == Runtime::FoolMode::ReverseAprilFools2026) {
        auto holdPiece = Piece::get_definition(Piece::fool_display_type(piece.type()));
        Render::draw_hold(context, holdPiece, player, Render::hold_draw_mode(context,player,1));
    } else {
        Render::draw_hold(context, piece, player, Render::hold_draw_mode(context,player,1));
    }
}

Rules::PlayResult command_signal(
    Runtime::GameContext& context,
    PlayerInputCommand command) {
    if (command == PlayerInputCommand::Retry || command == PlayerInputCommand::OpenSettings) {
        if(!Network::handle_room_game_control(context,command==PlayerInputCommand::Retry)) return Rules::PlayResult::ContinueFrame;
        if (context.config.garbage.mode == Runtime::GarbageMode::Multiplayer && !context.session.roomScene.enabled) {
            Network::send_round_signal(context, Network::OutgoingRoundSignal::LeaveRound);
        }
    }
    if (command == PlayerInputCommand::Retry) return Rules::PlayResult::Retry;
    if (command == PlayerInputCommand::OpenSettings) return Rules::PlayResult::OpenSettings;
    return Rules::PlayResult::ContinueFrame;
}

} // namespace

LivePieceControlResult process_live_piece_controls(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    MovementState& movementState,
    MovementInputState& inputState,
    AutoplayCarryState& autoplayCarry,
    bool heldPieceContinuation,
    Piece::Pose poseBeforeInput,
    const std::vector<int>& rotateKeys,
    const std::vector<int>& moveKeys,
    MovementController& movement,
    const MovementController::CanPlace& canPlace,
    const MovementController::TryRotate& tryRotate,
    const std::function<Rules::PlayResult()>& pumpFrame) {
    LivePieceControlResult result;
    Piece::Pose recordedPose = movementState.pose;

    if (Runtime::autoplay_enabled(context.config.autoplay.mode)) {
        const auto autoplayResult = context.config.autoplay.handlingSimulation
            ? run_handling_autoplay_step(context,field,piece,player,movementState,inputState,pumpFrame,&autoplayCarry,heldPieceContinuation)
            : run_autoplay_step(
            context,
            field,
            piece,
            player,
            movementState,
            autoplayCarry,
            heldPieceContinuation,
            context.config.autoplay.pps,
            Runtime::hold_available(context.config.field,player.gameplay.hold.used) && hold_replacement_available(context,player),
            context.config.quickPlay.fool,
            Runtime::current_gravity_interval(context),
            canPlace,
            tryRotate,
            pumpFrame);
        if (autoplayResult.signal != Rules::PlayResult::ContinueFrame) {
            result.signal = autoplayResult.signal;
            return result;
        }
        result.hardDrop = autoplayResult.hardDrop;
        result.locked = autoplayResult.locked;
        if(context.config.autoplay.handlingSimulation) { result.movementRecorded=true;return result; }
    }

    const PlayerInputConfig config{
            Runtime::autoplay_enabled(context.config.autoplay.mode),
            !(Runtime::hold_available(context.config.field,player.gameplay.hold.used) && hold_replacement_available(context,player)),
            context.config.field.holdEnabled,
            context.config.handling.softDropFactor,
            Runtime::current_gravity_interval(context),
            context.config.handling.das,
            context.config.handling.arr,
            Runtime::effective_lock_reset_limit(context.config.handling),
            context.config.movement,
            context.config.handling.dcd,
            context.config.handling.softDropFixed,
            context.config.disableUndo,
            false};
    auto& services = context.input_services();
    const auto recordMovement = [&] {
        if (recordedPose == movementState.pose) return;
        record_piece_pose(context, Replay::OperationKind::Move, movementState.pose);
        recordedPose = movementState.pose;
        result.movementRecorded = true;
    };
    const PlayerInputResult inputResult = services.gameplay_input_active()
        ? process_buffered_player_input_frame(inputState, movement, services, rotateKeys, moveKeys,
            config, context.real_now(),
            [&](auto time) { return context.real_time_at(time); }, recordMovement)
        : process_player_input_frame(inputState, movement, services.bindings, rotateKeys, moveKeys,
            config, [&](int key) { return services.key_down(key); },
            [&context] { return context.real_now(); });
    player.gameplay.scoring.score += inputResult.scoreDelta;
    if(context.config.challengeRules==Runtime::ChallengeRules::Audio && (movementState.pose!=poseBeforeInput || inputResult.command==PlayerInputCommand::HardDrop))
        Render::play_audio_cue(Render::AudioCue::Position,movementState.pose.position.col,movementState.pose.rotation.index());
    if (inputResult.command == PlayerInputCommand::Undo && undo_enabled(context) && undo_snapshot_count(context)>0)
        throw UndoRequested{};

    if (inputResult.command == PlayerInputCommand::Hold) {
        recordMovement();
        perform_live_hold(context, field, piece, player, poseBeforeInput, movementState.pose);
        result.signal = Rules::PlayResult::Hold;
        return result;
    }

    result.signal = command_signal(context, inputResult.command);
    if (result.signal != Rules::PlayResult::ContinueFrame) return result;
    if (inputResult.command == PlayerInputCommand::HardDrop) result.hardDrop = true;
    return result;
}

} // namespace Engine
