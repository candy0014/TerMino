#include "tetr/input/services.h"
#include "tetr/app/replay_playback.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/input/action.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/replay/movement_event.h"
#include "tetr/engine/replay_application.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/rules/game_result.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/turn_timeline.h"

#include <cassert>
#include <functional>

namespace App {
Rules::PlayResult playback_replay_piece(
    Runtime::GameContext& context,Runtime::Playfield& field,Piece::Definition& piece,
    Runtime::PlayerSessionState& player,Engine::MovementState& movementState,
    Engine::MovementInputState& inputState,const std::function<Rules::PlayResult()>& pumpFrame) {
    struct ActivePieceScope {
        Runtime::PlayerPresentationState& presentation;
        ~ActivePieceScope() {
            // A locked piece becomes board cells. The next replay piece must
            // not treat it as an overlay to erase, including repeated types.
            presentation.activePose.reset();presentation.activePieceType.reset();
            presentation.paintedActivePose.reset();presentation.paintedActivePieceType.reset();
        }
    } activeScope{player.presentation};
    while(1){
        Engine::update_replay_playback_controls(context,player);
        Replay::QueuedEvent event;
        if(Replay::pop_due(Replay::session(context).operations,Replay::playback_elapsed(context,context.now()),event)){
            Replay::mark_operation_consumed(context, event.operation);
            if(event.movement.type==Replay::MovementEventType::EndAll) {
                player.presentation.activePose.reset();
                player.presentation.activePieceType.reset();
                Engine::update_replay_playback_controls(context,player,true);
                return Rules::PlayResult::Dead;
            }
            Engine::apply_replay_gameplay_event(context,event.operation,field,player,piece);
            if(event.movement.type==Replay::MovementEventType::StartPlace) {
                player.presentation.activePieceBorn=event.operation.gameTimestamp.value_or(event.operation.timestamp);break;
            }
        } else {
            context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
        }
    }
    player.presentation.activePieceType=piece.type();
    player.presentation.activePose=movementState.pose;
    for(const auto& [key, action]:context.input_services().bindings.entries()){
        if(action==Input::Action::Retry||action==Input::Action::OpenSettings)
            inputState.setPhase(key,context.input_services().key_down(key)?Engine::KeyPhase::AwaitRelease:Engine::KeyPhase::Released);
    }
    while(1){
        Engine::update_replay_playback_controls(context,player);
        if(context.config.garbage.mode == Runtime::GarbageMode::Backfire||context.config.garbage.mode == Runtime::GarbageMode::Survival||context.config.garbage.mode == Runtime::GarbageMode::Multiplayer||context.config.garbage.mode == Runtime::GarbageMode::QuickPlay)
            Engine::GarbageService::update_buffer(context,player);
        if(Engine::periodic_event_due(context.now(),player.timing.frameUpdate,Runtime::Duration::from_seconds(0.05))){
            const auto signal=pumpFrame(); if(signal!=Rules::PlayResult::ContinueFrame) return signal;
        }
        if(context.config.garbage.mode == Runtime::GarbageMode::QuickPlay&&
           Replay::playback_qp_gravity_due(
               context,context.now(),Runtime::Duration::from_seconds(1.0))){
            Runtime::set_current_gravity_interval(
                context,
                Engine::advance_qp_gravity_speed(Runtime::current_gravity_interval(context), false));
        }
        Replay::QueuedEvent event;
        if(Replay::pop_due(Replay::session(context).operations,Replay::playback_elapsed(context,context.now()),event)){
            Replay::mark_operation_consumed(context, event.operation);
            if(event.movement.type==Replay::MovementEventType::EndAll) {
                player.presentation.activePose.reset();
                player.presentation.activePieceType.reset();
                Engine::update_replay_playback_controls(context,player,true);
                return Rules::PlayResult::Dead;
            }
            Engine::apply_replay_gameplay_event(context,event.operation,field,player,piece);
            if(event.movement.type==Replay::MovementEventType::Put1){
                movementState.pose=event.movement.pose;
                player.presentation.activePose=movementState.pose;
                assert(piece.can_place(movementState.pose,field));
            }
            if(event.movement.type==Replay::MovementEventType::Move){
                movementState.pose=event.movement.pose;
                player.presentation.activePose=movementState.pose;
                assert(piece.can_place(movementState.pose,field));
            }
            if(event.movement.type==Replay::MovementEventType::Hold){
                player.presentation.activePose.reset();
                player.presentation.activePieceType.reset();
                return Rules::PlayResult::Hold;
            }
            if(event.movement.type==Replay::MovementEventType::EndMove) break;
        }
        for(const auto& [key, action]:context.input_services().bindings.entries()){
            const bool down=context.input_services().key_down(key);
            if(inputState.consumeReleaseDebounce(key,down)) continue;
            if(action==Input::Action::Retry&&down) return Rules::PlayResult::Retry;
            if(action==Input::Action::OpenSettings&&down) return Rules::PlayResult::OpenSettings;
        }
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
    while(1){
        Engine::update_replay_playback_controls(context,player);
        Replay::QueuedEvent event;
        if(Replay::pop_due(Replay::session(context).operations,Replay::playback_elapsed(context,context.now()),event)){
            Replay::mark_operation_consumed(context, event.operation);
            if(event.movement.type==Replay::MovementEventType::EndAll) {
                player.presentation.activePose.reset();
                player.presentation.activePieceType.reset();
                Engine::update_replay_playback_controls(context,player,true);
                return Rules::PlayResult::Dead;
            }
            assert(event.movement.type==Replay::MovementEventType::EndMoveData);
            movementState.lastRotation=event.movement.lastRotation;
            movementState.inputActions=event.movement.inputActions;
            player.gameplay.scoring.score=event.movement.score;
            assert(movementState.pose==event.movement.pose);
            player.presentation.activePose.reset();
            player.presentation.activePieceType.reset();
            break;
        }
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
    return Rules::PlayResult::ContinueFrame;
}
} // namespace App
