#include "tetr/input/services.h"
#include "tetr/engine/live_piece_controller.h"

#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/live_piece_periodic_events.h"
#include "tetr/rules/game_completion.h"
#include "tetr/rules/spin.h"
#include "tetr/engine/movement_controller.h"
#include "live_piece_controls.h"
#include "tetr/engine/player_input.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/undo.h"
#include "piece_replay.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/replay/recorder.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/player_session_state.h"

#include <functional>
#include <vector>
#include <algorithm>
#include "tetr/engine/periodic_events.h"

namespace Engine {

Rules::PlayResult run_live_piece_movement(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    MovementState& movementState,
    MovementInputState& inputState,
    AutoplayCarryState& autoplayCarry,
    bool heldPieceContinuation,
    const MovementController::CanPlace& canPlace,
    const MovementController::TryRotate& tryRotate,
    const std::function<Rules::PlayResult()>& pumpFrame) {
    struct ActivePieceScope {
        Runtime::PlayerPresentationState& presentation;
        ~ActivePieceScope() {
            presentation.activePose.reset(); presentation.activePieceType.reset();
            presentation.paintedActivePose.reset();
            presentation.paintedActivePieceType.reset();
        }
    } activeScope{player.presentation};
    player.presentation.activePieceType=piece.type();
    player.presentation.activePose=movementState.pose;
    auto& scoring = player.gameplay.scoring;
    const auto layoutRevision=[&] {
        std::unique_lock<std::recursive_mutex> lock;
        if (context.output_mutex()) lock=std::unique_lock<std::recursive_mutex>(*context.output_mutex());
        return player.presentation.terminalLayoutRevision;
    };
    Runtime::TimePoint& lockTimer = movementState.lockTimer;
    MovementController movement(movementState, canPlace, tryRotate,[&](MovementContact contact,int direction,Runtime::TimePoint at) {
        auto& feedback=player.presentation.boardMotion;
        if(contact==MovementContact::Wall) {
            // ARR=0 may probe the wall on every input poll. Presentation
            // impulses have a fixed rate, independent of the poll/render rate.
            auto& last=player.presentation.lastWallContact;
            if(last && at>=*last && direction==feedback.wallDirection && at-*last<Runtime::Duration::from_seconds(1./60))return;
            last=at;++feedback.wallSequence;feedback.wallDirection=direction;
        }
        else {
            // The reference tilts on a recognized Spin. Consume the shared
            // predicate here; presentation never reconstructs a spin rule.
            const auto metadata=Piece::metadata(piece.type());
            Rules::SpinInput input;
            input.spinChecker=metadata.spinCheck;input.pose=movementState.pose;
            input.lastRotation=movementState.lastRotation;
            input.mapWidth=context.config.field.width;input.mapHeight=player.gameplay.playfield.activeHeight;
            input.grounded=!canPlace(movementState.pose.translated(1,0));input.immobile=true;
            for(const auto& shift:std::array<std::array<int,2>,4>{{{{1,0}},{{0,1}},{{-1,0}},{{0,-1}}}})
                if(canPlace(movementState.pose.translated(shift[0],shift[1]))){input.immobile=false;break;}
            input.allSpinEnabled=Runtime::enabled(context.config.quickPlay.allSpin);
            input.noHoldReverse=Runtime::reverse(context.config.quickPlay.noHold);
            input.bonuses=context.config.spinBonuses;input.rotationSystem=metadata.rotationSystem;
            input.canonicalGeometry=metadata.tetrioRotation;
            if(Runtime::reverse(context.config.quickPlay.doubleHole) && !Piece::t_spin_check(metadata.spinCheck))input.lastRotation={};
            if(Rules::detect_spin(input,[&](int row,int col){return field.cell(row,col).occupied();}).spin) {
                ++feedback.rotationSequence;feedback.rotationDirection=direction;
            }
        }
    });
    bool autoplayContinuation=heldPieceContinuation;

    Replay::add_replay(context, Replay::OperationKind::StartPlace);
    const bool instantGravityAlreadyMovesPiece =
        Runtime::current_gravity_interval(context).is_instant() &&
        piece.can_place(movementState.pose.translated(1, 0), field);
    bool pieceDrawn = !instantGravityAlreadyMovesPiece;
    Piece::Pose renderedPose = movementState.pose;
    auto paintedRevision=player.presentation.activePiecePaintRevision;
    Runtime::TimePoint lastRender = context.real_now();
    const auto renderInterval = Runtime::Duration::from_seconds(1.0/120.0);
    const auto synchronizePaintedPiece = [&] {
        std::unique_lock<std::recursive_mutex> lock;
        if (context.output_mutex()) lock=std::unique_lock<std::recursive_mutex>(*context.output_mutex());
        const auto& p=player.presentation;
        if (paintedRevision==p.activePiecePaintRevision) return;
        paintedRevision=p.activePiecePaintRevision;
        pieceDrawn=p.paintedActivePose.has_value();
        if (pieceDrawn) renderedPose=*p.paintedActivePose;
        lastRender=context.real_now();
    };
    const auto presentPiece = [&] {
        {
            Render::OutputGuard output(context);
            synchronizePaintedPiece();
            player.presentation.activePose=movementState.pose;
            if (!pieceDrawn) Render::draw_piece(context,piece,player,movementState.pose,field);
            else if (renderedPose != movementState.pose)
                Render::draw_piece_transition(context,piece,player,renderedPose,movementState.pose,field);
            renderedPose=movementState.pose;
            pieceDrawn=true;
            paintedRevision=player.presentation.activePiecePaintRevision;
            lastRender=context.real_now();
        }
        // Commit the local movement before serialization or socket work.
        if (context.config.garbage.mode==Runtime::GarbageMode::Multiplayer)
            Network::send_piece_motion_frame(context,player,movementState.pose);
    };
    if (!instantGravityAlreadyMovesPiece) {
        record_piece_pose(context, Replay::OperationKind::PutDraw, movementState.pose);
        Render::draw_piece(context, piece, player, movementState.pose, field);
        if (context.config.garbage.mode==Runtime::GarbageMode::Multiplayer)
            Network::send_piece_motion_frame(context,player,movementState.pose);
        synchronizePaintedPiece();
    }

    lockTimer = context.real_now();
    inputState.preparePiece(context.input_services().bindings,
        [&context](int key) {
            auto& services=context.input_services();
            return services.gameplay_input_active() ? services.gameplay_key_down(key) : services.key_down(key);
        },lockTimer);
    const std::vector<int> rotateKeys = collect_action_keys(
        context.input_services().bindings,
        {Input::Action::Rotate180, Input::Action::RotateClockwise, Input::Action::RotateCounterclockwise});
    const std::vector<int> moveKeys = collect_action_keys(
        context.input_services().bindings,
        {Input::Action::MoveLeft, Input::Action::MoveRight});

    for (;;) {
        poll_undo_key(context);
        synchronizePaintedPiece();
        // Field-changing/UI updates need the last visible piece synchronized.
        const bool periodicDue = periodic_event_due(context.real_now(),player.timing.framePoll,Network::multiplayer_frame_poll_interval(context)) ||
            (context.config.garbage.mode==Runtime::GarbageMode::CheeseTimer &&
                periodic_event_due(context.now(),player.timing.timedGarbage,context.config.garbage.generationInterval)) ||
            Rules::time_goal_reached(context.config,Runtime::elapsed_game_time(context.session,context.now())) ||
            GarbageService::has_due_garbage_rise(context,player) ||
            (context.config.garbage.entryMode == Runtime::GarbageEntryMode::Immediate && GarbageService::has_ready_buffer(context,player));
        if (periodicDue && pieceDrawn && renderedPose!=movementState.pose) presentPiece();
        const auto poseBeforePeriodic=movementState.pose;
        const auto garbageTimeBefore=player.timing.timedGarbage;
        player.presentation.activePose=movementState.pose;
        const auto layoutBefore=layoutRevision();
        bool bufferRisen=false;
        if (const auto signal = process_live_piece_periodic_events(
                context, field, piece, player, movementState,
                heldPieceContinuation && Runtime::autoplay_enabled(context.config.autoplay.mode),
                pumpFrame,pieceDrawn,&bufferRisen)) {
            return *signal;
        }
        if (bufferRisen && !autoplayCarry.selectedPieceType.valid())autoplayContinuation=false;
        if (bufferRisen || poseBeforePeriodic!=movementState.pose || garbageTimeBefore!=player.timing.timedGarbage) {
            renderedPose=movementState.pose;
            pieceDrawn=true;
            lastRender=context.real_now();
        }
        if (layoutBefore!=layoutRevision()) pieceDrawn=false;
        // A repaint includes the active piece and ghost. Use its actual pose
        // as the next transition's origin, rather than drawing over it again.
        synchronizePaintedPiece();

        const Runtime::TimePoint movementNow = context.real_now();
        const auto gravity=Runtime::current_gravity_interval(context);
        bool softDropOwnsGravity=false;
        if (!Runtime::autoplay_enabled(context.config.autoplay.mode) && !context.config.movement.disableSoftDrop) {
            for (const auto& [key,action]:context.input_services().bindings.entries()) {
                if (action!=Input::Action::SoftDrop) continue;
                auto& services=context.input_services();
                const bool down=services.gameplay_input_active() ? services.gameplay_key_down(key) : services.key_down(key);
                if (down && (context.config.handling.softDropFixed || context.config.handling.softDropFactor==0 ||
                    context.config.handling.softDropFactor>=1)) softDropOwnsGravity=true;
            }
        }
        const auto gravityEffect = Runtime::autoplay_enabled(context.config.autoplay.mode) || gravity.is_instant() ||
            (softDropOwnsGravity && !gravity.is_instant())
            ? MovementEffect{} : movement.applyGravity(movementNow,gravity);
        if (gravityEffect.changed) {
            record_piece_pose(context, Replay::OperationKind::Move, movementState.pose);
        }

        const Piece::Pose poseBeforeInput = movementState.pose;
        const LivePieceControlResult controlResult = process_live_piece_controls(
            context,
            field,
            piece,
            player,
            movementState,
            inputState,
            autoplayCarry,
            autoplayContinuation,
            renderedPose,
            rotateKeys,
            moveKeys,
            movement,
            canPlace,
            tryRotate,
            pumpFrame);
        synchronizePaintedPiece();
        if (controlResult.signal != Rules::PlayResult::ContinueFrame) return controlResult.signal;
        const bool hardDrop = controlResult.hardDrop;
        if (!hardDrop && gravity.is_instant() && Runtime::autoplay_enabled(context.config.autoplay.mode))
            movement.applyGravity(context.real_now(),gravity);

        if (movementState.pose != poseBeforeInput || hardDrop) {
            if (!controlResult.movementRecorded)
                record_piece_pose(context, Replay::OperationKind::Move, movementState.pose);
        }
        // Buffered rotations/moves get their lock resets before a stalled
        // frame can lock the piece using an out-of-date timer.
        const bool locking = hardDrop || controlResult.locked || movement.shouldLock(
                context.real_now(), Runtime::current_epld(context),
                Runtime::current_gravity_interval(context),
                Runtime::autoplay_enabled(context.config.autoplay.mode));
        if (locking || !pieceDrawn || context.real_now()-lastRender>=renderInterval) {
            if (!pieceDrawn || renderedPose!=movementState.pose) presentPiece();
        }
        if (locking) {
            if(hardDrop)movementState.hardDropFromRow=poseBeforeInput.position.row;
            Replay::add_replay(context, Replay::OperationKind::EndMove);
            break;
        }
        auto& services=context.input_services();
        if (services.gameplay_input_active() && !Runtime::autoplay_enabled(context.config.autoplay.mode)) {
            const auto now=context.real_now();
            auto wait=Runtime::Duration::from_seconds(.01);
            const auto consider=[&](Runtime::TimePoint deadline) {
                wait=std::min(wait,std::max(deadline-now,Runtime::Duration::from_seconds(.001)));
            };
            consider(player.timing.framePoll+Network::multiplayer_frame_poll_interval(context));
            if (player.garbage.rising.remainingLines>0 && player.garbage.rising.nextRow)
                consider(now+(*player.garbage.rising.nextRow-context.now()));
            if (renderedPose!=movementState.pose) consider(lastRender+renderInterval);
            // Sampled sources wake this wait when their next complete batch
            // is published. Waking for an unobserved repeat deadline would
            // only spin while the input clock is intentionally bounded.
            if (!services.gameplay_sample_time()) {
                const int moveKey=inputState.mostRecentActive(moveKeys);
                if (const auto due=inputState.nextHorizontalRepeat(moveKey,context.config.handling.das,context.config.handling.arr,now)) consider(*due);
                for (const auto& [key,action]:services.bindings.entries()) {
                    if (action!=Input::Action::SoftDrop || context.config.movement.disableSoftDrop || !services.gameplay_key_down(key)) continue;
                    const auto interval=soft_drop_interval(gravity,context.config.handling.softDropFactor,context.config.handling.softDropFixed);
                    if (interval>Runtime::Duration{}) consider(inputState.timestamp(key)+interval);
                }
            }
            if (gravity.enabled()) {
                if (movement.grounded()) consider(lockTimer+Runtime::current_epld(context));
                else if (!softDropOwnsGravity && !gravity.is_instant()) consider(lockTimer+gravity.interval());
            }
            services.wait_for_gameplay_input(context.services.clock.wall_duration(wait));
        } else context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }

    if (context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) {
        Network::send_board_state_frame(
            context, player, Network::ActivePieceState::active(movementState.pose));
    }
    Replay::record_end_move_data(
        context, movementState.lastRotation, scoring.score, movementState.pose,
        Runtime::finesse_rules(context.config.challengeRules)?movementState.inputActions:-1);
    return Rules::PlayResult::ContinueFrame;
}

} // namespace Engine
