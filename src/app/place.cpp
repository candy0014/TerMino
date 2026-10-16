#include "tetr/app/place.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/rules/blitz.h"
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/live_piece_controller.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/entry_delay.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/app/replay_playback.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/replay/session_state.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/audio_cues.h"

namespace App {
using Result = Rules::PlayResult;
Result run_frame(Runtime::GameContext& context,Runtime::Playfield& field,Runtime::PlayerSessionState& player){
    return Engine::run_frame(context,field,player);
}
Result run_piece(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition piece,
    Runtime::PlayerSessionState& player,
    PieceLoopState& loopState,
    bool holdContinuation) {
    if(context.config.mode.replayMode && context.replay_component()) {
        const auto& replay=Replay::session(context);
        if(replay.challengePrefixActive && !replay.operations.empty() &&
           replay.operations.front().kind==Replay::OperationKind::EndAll)return Result::Dead;
    }
    player.gameplay.scoring.roundPieceTypes.insert(piece.type().index());
    int blitzLevel=0;
    if (!holdContinuation) {
        const auto result = Engine::wait_for_entry_delay(context,player,
            [&](){return run_frame(context,field,player);},&loopState.input);
        if (result != Result::ContinueFrame) return result;
    }
    if(context.config.garbage.mode!=Runtime::GarbageMode::QuickPlay &&
        (context.config.handling.gravityProgression==Runtime::GravityProgression::Blitz ||
        (context.config.mode.mode==Runtime::GameMode::Blitz && !context.config.mode.fixedBlitzSpeed))){
        blitzLevel=Rules::blitz_level_from_lines(player.gameplay.scoring.totalLines);
        Runtime::set_current_gravity_interval(
            context, Runtime::GravityInterval::periodic(Rules::blitz_gravity_for_level(blitzLevel)));
    } else if(context.config.garbage.mode!=Runtime::GarbageMode::QuickPlay &&
        context.config.handling.gravityProgression!=Runtime::GravityProgression::Fixed) {
        Runtime::set_current_gravity_interval(context,Runtime::progressed_gravity(
            context.config.handling.gravityProgression,player.gameplay.scoring.totalLines,context.config.handling.gravityInterval,
            context.config.handling.gravityLevelLines,context.config.handling.gravityAcceleration));
    }
    Engine::MovementState movementState=Engine::make_spawn_movement_state(context.config.field.width,piece.type());
    player.presentation.activePieceBorn=Runtime::Duration::from_seconds(Render::board_elapsed_seconds(context,player));
    player.presentation.activePieceShown=true;
    if(!context.config.mode.replayMode && context.config.challengeRules==Runtime::ChallengeRules::Audio)
        Render::play_audio_cue(Render::AudioCue::Piece,Piece::metadata(piece.type()).familyIndex);
    auto canPlace=[&piece,&field](Piece::Pose pose){return piece.can_place(pose,field);};
    auto tryRotate=[&piece,&field](Piece::Pose& pose,Runtime::Rotation target,Piece::KickIndex& kickIndex){
        return piece.try_rotate(pose,target,field,kickIndex);
    };
    if (!context.config.mode.replayMode)
        Engine::apply_initial_movement(context,loopState.input,movementState,canPlace);
    if(Engine::handle_spawn_collision(context,field,piece,player,movementState)) return Result::Dead;

    if(!context.config.mode.replayMode){
        const auto signal=Engine::run_live_piece_movement(
            context,field,piece,player,movementState,loopState.input,loopState.autoplay,
            holdContinuation,canPlace,tryRotate,[&](){return Engine::run_frame(context,field,player,true,false);});
        if(signal!=Result::ContinueFrame) return signal;
    } else {
        const auto signal=App::playback_replay_piece(
            context,field,piece,player,movementState,loopState.input,
            [&](){return run_frame(context,field,player);});
        if(signal!=Result::ContinueFrame) return signal;
    }

    return Engine::finalize_locked_piece(
        context,field,piece,player,movementState,blitzLevel,
        [&](){return Engine::run_frame(context,field,player,true,false);},&loopState.input);
}

} // namespace App
