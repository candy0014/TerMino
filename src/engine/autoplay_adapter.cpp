#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/autoplay_path.h"

#include "tetr/engine/movement_controller.h"
#include "tetr/engine/live_piece_periodic_events.h"
#include "piece_replay.h"
#include "tetr/engine/movement_presentation.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/undo.h"
#include <algorithm>
#include "tetr/autoplay/strategy.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/tetrio_recorder.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/player_session_state.h"

#include <cassert>
#include <cmath>
#include <functional>
#include <string>

namespace Engine {

bool initialize_autoplay(Runtime::GameContext& context) {
    return context.autoplay_component() && context.autoplay_component()->initialize(context);
}

namespace {


std::vector<Piece::Type> bot_piece_sequence(const Runtime::PlayerSessionState& player) {
    std::vector<Piece::Type> sequence;
    sequence.reserve(player.pieces.nextPreview.size() + 1);
    sequence.push_back(Engine::current_piece(player));
    sequence.insert(sequence.end(), player.pieces.nextPreview.begin(), player.pieces.nextPreview.end());
    return sequence;
}

void present_autoplay_hold(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    Piece::Pose previousPose,
    Runtime::FoolMode foolMode) {
    record_piece_pose(context, Replay::OperationKind::PutErase, previousPose);
    if (player.presentation.paintedActivePose)
        Render::draw_piece(context,piece,player,*player.presentation.paintedActivePose,field,Render::PieceDrawMode::Erase);
    Replay::add_replay(context, Replay::OperationKind::Hold);
    if (auto* recorder = Replay::tetrio_recorder(context)) recorder->hold();

    if (player.gameplay.hold.piece.valid()) {
        Render::draw_board_hold(context, player, Render::PieceDrawMode::Erase);
    }
    if (foolMode == Runtime::FoolMode::ReverseAprilFools2026) {
        auto displayPiece = Piece::get_definition(Piece::fool_display_type(piece.type()));
        Render::draw_hold(context, displayPiece, player, Render::hold_draw_mode(context,player,1));
    } else {
        Render::draw_hold(context, piece, player, Render::hold_draw_mode(context,player,1));
    }
}

} // namespace

AutoplayStepResult run_autoplay_step(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    MovementState& movementState,
    AutoplayCarryState& carry,
    bool heldPieceContinuation,
    double pps,
    bool holdEnabled,
    Runtime::FoolMode foolMode,
    Runtime::GravityInterval gravityInterval,
    const MovementController::CanPlace& canPlace,
    const MovementController::TryRotate& tryRotate,
    const std::function<Rules::PlayResult()>& pumpFrame) {
    AutoplayStepResult result;
    if (!context.autoplay_component()) return result;
    const Piece::Pose previousPose = movementState.pose;
    const auto previousRotation = movementState.lastRotation;
    Piece::Pose targetPose = previousPose;
    Runtime::RotationRecord targetRotation = previousRotation;
    auto& hold = player.gameplay.hold;

    if (!heldPieceContinuation) {
        const Autoplay::HoldInput botHold{holdEnabled, hold.piece};
        const auto botSequence = bot_piece_sequence(player);
        const auto unseenBagPieces = Engine::unseen_pieces_in_preview_endpoint_bag(context, player);
        carry.selectedPieceType = context.autoplay_component()->choose_position(
            context,
            player,
            field,
            botSequence,
            unseenBagPieces,
            movementState.pose,
            botHold,
            targetPose,
            targetRotation);
        if (carry.selectedPieceType != piece.type()) {
            carry.pose = targetPose;
            carry.lastRotation = targetRotation;
            present_autoplay_hold(
                context, field, piece, player, previousPose, foolMode);
            result.signal = Rules::PlayResult::Hold;
            return result;
        }
    } else if (carry.selectedPieceType == piece.type()) {
        targetPose = carry.pose;
        targetRotation = carry.lastRotation;
    } else {
        const auto botSequence = bot_piece_sequence(player);
        const auto unseenBagPieces = Engine::unseen_pieces_in_preview_endpoint_bag(context, player);
        context.autoplay_component()->choose_position(
            context,
            player,
            field,
            botSequence,
            unseenBagPieces,
            movementState.pose,
            Autoplay::HoldInput{false, Piece::Type::none()},
            targetPose,
            targetRotation);
    }

    assert(piece.can_place(targetPose, field));
    assert(!piece.can_place(targetPose.translated(1, 0), field));

    const auto softInterval=soft_drop_interval(gravityInterval,context.config.handling.softDropFactor,
        context.config.handling.softDropFixed);
    const bool slowPresentation=pps>1e-8 && pps<=5;

    // Computing and presenting happen inside the placement period. Entry and
    // clear delays since the previous lock have already spent part of it.
    // Keep frame/input/garbage processing live throughout every wait.
    const auto service=[&]() {
        poll_undo_key(context);
        const int activeHeightBefore=player.gameplay.playfield.activeHeight;
        if (periodic_event_due(context.real_now(),player.timing.framePoll,Runtime::Duration::from_seconds(.05))) {
            result.signal=pumpFrame();
            if (result.signal!=Rules::PlayResult::ContinueFrame) return false;
        }
        bool fieldChanged=false;
        if (const auto signal=apply_immediate_garbage_to_live_piece(
                context,field,piece,player,movementState,player.presentation.paintedActivePose.has_value(),&fieldChanged)) {
            result.signal=*signal;
            return false;
        }
        if (fieldChanged || activeHeightBefore!=player.gameplay.playfield.activeHeight) {
            carry={}; // Replan against the changed field, using the same PPS slot.
            return false;
        }
        return true;
    };
    const auto waitUntil=[&](Runtime::TimePoint deadline) {
        while (context.real_now()<deadline) {
            if (!service()) return false;
            const auto remaining=deadline-context.real_now();
            context.services.clock.sleep_for(std::min(remaining,Runtime::Duration::from_seconds(.001)));
        }
        return service();
    };

    if (slowPresentation) {
        std::string script=context.autoplay_component()->movement_script(
            context,field,piece,previousPose,targetPose,targetRotation);
        if (script.empty()) {
            auto floor=previousPose;
            while (canPlace(floor.translated(1,0))) ++floor.position.row;
            if (floor==targetPose && !context.config.movement.disableHardDrop) script="H";
            else {
                movementState.pose=previousPose;
                movementState.lastRotation=previousRotation;
                return result;
            }
        }
        movementState.pose=previousPose;
        movementState.lastRotation=Runtime::RotationRecord::none();
        auto* tetrioRecorder=Replay::tetrio_recorder(context);
        if (tetrioRecorder) tetrioRecorder->begin_script(script.size()+field.height()+Runtime::kHiddenRowCapacity);
        MovementScriptRecorder recordInput;
        if (tetrioRecorder) recordInput=[&](char command,int rows,int columns) { tetrioRecorder->movement(command,rows,columns); };
        const auto commandInterval=Runtime::Duration::from_seconds(
            slowPresentation ? std::min(.15,.4/pps)/script.size() : 0);
        auto nextCommand=context.real_now();
        const auto present=[&](MovementScriptCommand command) {
            const auto previous=movementState.pose;
            const auto effect=apply_movement_script_command(movementState,command,canPlace,tryRotate,
                context.config.handling.das==Runtime::Duration{},context.config.handling.arr==Runtime::Duration{},
                context.config.movement,gravityInterval.is_instant(),softInterval==Runtime::Duration{},recordInput);
            if (effect.changed) {
                record_piece_pose(context,Replay::OperationKind::Move,movementState.pose);
                if (player.presentation.paintedActivePose)
                    Render::draw_piece_transition(context,piece,player,*player.presentation.paintedActivePose,movementState.pose,field);
                else Render::draw_piece(context,piece,player,movementState.pose,field);
            }
        };
        for (const char code:script) {
            const auto command=decode_movement_script_command(code);
            if (command==MovementScriptCommand::GravityStep)
                nextCommand=std::max(nextCommand,context.real_now()+gravityInterval.interval());
            if (command==MovementScriptCommand::WaitForLock)
                nextCommand=std::max(nextCommand,context.real_now()+context.config.handling.epld);
            if (!waitUntil(nextCommand)) return result;
            if (command==MovementScriptCommand::WaitForLock) break;
            if (command==MovementScriptCommand::HardDrop) {
                // Finish locking after the cadence wait below.
                const auto previous=movementState.pose;
                while (canPlace(movementState.pose.translated(1,0))) {
                    ++movementState.pose.position.row;
                    movementState.lastRotation=Runtime::RotationRecord::none();
                }
                if (previous!=movementState.pose) {
                    record_piece_pose(context,Replay::OperationKind::Move,movementState.pose);
                    if (player.presentation.paintedActivePose)
                        Render::draw_piece_transition(context,piece,player,*player.presentation.paintedActivePose,movementState.pose,field);
                    else Render::draw_piece(context,piece,player,movementState.pose,field);
                }
                break;
            }
            present(command);
            nextCommand=context.real_now()+(command==MovementScriptCommand::SoftDropStep ||
                command==MovementScriptCommand::GravityStep ? Runtime::Duration{} : commandInterval);
        }
        assert(movementState.pose==targetPose);
        assert(autoplay_landing_matches(field,piece,movementState,targetPose,targetRotation,autoplay_spin_rules(context.config)));
    }

    if (pps>1e-8) {
        const auto period=Runtime::Duration::from_seconds(1/pps);
        const auto deadline=player.timing.autoplay+period;
        if (!waitUntil(deadline)) return result;
        const auto now=context.real_now();
        // Preserve cadence across small scheduler overshoots; discard missed
        // slots when mandatory work overruns, preventing an unbounded backlog.
        player.timing.autoplay=now-deadline>=period ? now : deadline;
    } else {
        if (!service()) return result;
        player.timing.autoplay=context.real_now();
    }

    // A planned landing has no terminal overlay yet. Service garbage and
    // cadence waits at the live pose; only commit the landing after they pass.
    // A rise invalidates the plan and lifts/redraws that same live piece.
    if (!slowPresentation) {
        movementState.pose=targetPose;
        movementState.lastRotation=targetRotation;
    }
    result.hardDrop = !context.config.movement.disableHardDrop;
    result.locked = context.config.movement.disableHardDrop && gravityInterval.enabled() &&
        !canPlace(movementState.pose.translated(1,0));
    return result;
}

} // namespace Engine
