#include "tetr/engine/frame_coordinator.h"
#include "tetr/app/application_state.h"
#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/engine/frame_board_adapter.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/spike_presentation.h"
#include "tetr/engine/undo.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/network/frame_pump.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/rules/frame_update.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/field_visibility.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"

#include <cstdio>
#include <cmath>

namespace Engine {

bool synchronize_field_visibility(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    const auto elapsed=Runtime::Duration::from_seconds(Render::board_elapsed_seconds(context,player));
    const auto& config=context.config.field;
    const bool flash=config.invisible && config.periodicInvisible && Runtime::periodic_invisible_flash(
        elapsed,config.invisibilityInterval,config.invisibilityDuration);
    bool changed=flash!=player.presentation.invisibleFlash;
    const bool shown=Render::active_piece_visible(context,player);
    changed=changed || shown!=player.presentation.activePieceShown;
    player.presentation.activePieceShown=shown;
    if(config.invisible && !config.periodicInvisible && config.invisibilityDelay>Runtime::Duration{}) {
        const auto previous=player.presentation.visibilityTime;
        const auto& field=player.gameplay.playfield.field;
        changed=changed || elapsed<previous;
        for(int row=-Runtime::kDisplayHiddenRows;row<field.height() && !changed;++row)
            for(int column=0;column<field.width();++column) {
                if(!field.cell(row,column).is_piece()) continue;
                const auto deadline=field.cell_lock_time(row,column)+config.invisibilityDelay;
                if(previous<deadline && deadline<=elapsed) {changed=true;break;}
            }
    }
    player.presentation.invisibleFlash=flash;
    player.presentation.visibilityTime=elapsed;
    return changed;
}

static void render_frame_status(Runtime::PlayerSessionState& player,const Runtime::GameContext& context){
    Render::OutputGuard output(context);
    Render::print_message(context,player);
    if(context.config.garbage.mode == Runtime::GarbageMode::QuickPlay&&Runtime::reverse(context.config.quickPlay.doubleHole)) Render::print_blighted(context,player);
    if(context.config.garbage.mode == Runtime::GarbageMode::QuickPlay) Render::print_altitude(context,player);
    else if(!context.session.roomScene.enabled) Render::print_score(context,player);
    fflush(stdout);
}

Rules::PlayResult run_frame(Runtime::GameContext& context,Runtime::Playfield& field,Runtime::PlayerSessionState& player,bool pollNetwork,bool allowImmediateGarbage){
    poll_undo_key(context);
    refresh_game_terminal_layout(context);
    Rules::PlayResult result = Rules::PlayResult::ContinueFrame;
    if(!context.config.mode.replayMode)if(auto* app=context.app_component();app && app->challengeFrame && app->challengeFrame())return Rules::PlayResult::Won;
    const Runtime::TimePoint now=context.now();
    const Runtime::Duration dtime=context.config.mode.replayMode
        ? Replay::consume_playback_frame_delta(context,now)
        : now-player.timing.frameUpdate;
    Runtime::Duration elapsedGameTime{};
    if(context.config.garbage.mode == Runtime::GarbageMode::QuickPlay) elapsedGameTime=context.config.mode.replayMode
        ? Replay::playback_game_elapsed(context,now) : Runtime::elapsed_game_time(context.session, now);

    Rules::FrameRuleState frameState=capture_frame_rule_state(player,context);
    const Rules::FrameRuleConfig frameConfig=make_frame_rule_config(context,elapsedGameTime);
    const Rules::FramePrePresentationResult preUpdate=
        Rules::advance_frame_before_presentation(frameState,frameConfig,dtime,context.services.random);
    store_frame_rule_state(player,frameState,context);

    if(context.config.garbage.mode == Runtime::GarbageMode::QuickPlay&&!context.config.mode.replayMode) Render::set_ui_color(Render::ColorRole::Text,context.config.display.obscuredColors);
    for (int attack:preUpdate.resolvedQpGarbagePackets)
        GarbageService::add_buffer(context,player,field,attack,false,attack,false,0);
    for(int attack:preUpdate.incomingGarbagePackets) {
        if (context.config.garbage.mode==Runtime::GarbageMode::QuickPlay)
            GarbageService::add_qp_opponent_attack(context,player,field,attack);
        else GarbageService::add_buffer(context,player,field,attack);
    }
    if (preUpdate.permanentGarbageLines) GarbageService::add_solid_garbage(
        context,player,field,preUpdate.permanentGarbageLines);

    if(pollNetwork&&!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer){
      if(context.session.roomScene.enabled) {
        auto& n=Network::network_services(context);Engine::poll_multiplayer_scene(context);
        if(n.matchEpoch!=context.session.roomScene.epoch || n.matchCanceled) throw Network::LobbyInterrupted("Match changed");
        if(n.matchPhase==Network::RoomMatchPhase::Finished && n.winnerSlot==n.roomPlayerIndex) result=Rules::PlayResult::Won;
        else if(!context.session.roomScene.selfAlive) result=Rules::PlayResult::Dead;
      } else {
        Network::resend_piece_turn(context);
        Network::FramePumpCallbacks callbacks;
        auto& opponent=context.session.opponent;
        callbacks.receive=[&context,&opponent](){
            return Network::try_receive_frame(
                context,opponent.gameplay.playfield.width,opponent.gameplay.playfield.visibleHeight);
        };
        callbacks.addIncomingAttack=[&](int attack){
            // Attack packets are already post-cancellation.  They are also the
            // only authoritative source for an online remote player's spike.
            // Local Versus renders both real players through separate game
            // contexts, so its unlaid-out opponent mirror must stay hidden.
            if(context.config.mode.mode == Runtime::GameMode::Multiplayer){
                present_spike_attack(context,opponent,attack);
            }
            GarbageService::add_buffer(context,player,field,attack);
        };
        callbacks.applyOpponentState=[&context,&opponent](const Network::BoardStateSnapshot& snapshot){
            Engine::apply_remote_board_state(context,opponent,snapshot);
        };
        if (Network::hybrid_board_sync_enabled(context) && !player.presentation.activePose &&
            context.real_now()-Network::network_services(context).boardSync.lastSnapshot>=Runtime::Duration::from_seconds(0.5)) {
            const auto& lastPlacement=Network::network_services(context).boardSync.lastPlacement;
            Network::send_board_state_frame(context,player,lastPlacement ? Network::ActivePieceState::locked() : Network::ActivePieceState::spawn(),lastPlacement);
        }
        callbacks.applyOpponentMotion=[&context,&opponent](const Network::PieceMotionFrame& motion) {
            if (!Engine::apply_remote_piece_motion(context,opponent,motion)) Network::request_board_snapshot(context);
        };
        auto& multiplayer=player.multiplayer;
        const Network::MultiplayerFrameSignal signal=Network::pump_multiplayer_frame(
            Network::MultiplayerFrameStateRef{multiplayer.pendingSends,multiplayer.receiveId,player.gameplay.scoring.wins},callbacks);
        if(signal==Network::MultiplayerFrameSignal::OpponentLeft) result = Rules::PlayResult::OpponentLeft;
        if(signal==Network::MultiplayerFrameSignal::Won) result = Rules::PlayResult::Won;
      }
    }

    if (Runtime::uses_garbage_buffer(context.config.garbage.mode)) {
        if (allowImmediateGarbage && !context.config.mode.replayMode) {
            if (context.config.garbage.entryMode == Runtime::GarbageEntryMode::Immediate)
                GarbageService::raise_ready_buffer(context,player,field);
            else GarbageService::advance_garbage_rise(context,player,field);
        }
        GarbageService::update_buffer(context,player);
    }
    refresh_spike_display(context,player);
    Render::print_windup(context,player);
    if(!context.config.mode.replayMode&&
       context.config.mode.mode == Runtime::GameMode::Multiplayer && !context.session.roomScene.enabled){
        refresh_spike_display(context,context.session.opponent);
    }
    render_frame_status(player,context);
    if (synchronize_field_visibility(context,player)) {
        Render::redraw_player_board(context,player);
    }
    frameState=capture_frame_rule_state(player,context);
    Rules::advance_frame_after_presentation(frameState,frameConfig,dtime);
    store_frame_rule_state(player,frameState,context);
    player.timing.frameUpdate=context.now();
    const auto pollNow=context.real_now();
    const auto pollInterval=Network::multiplayer_frame_poll_interval(context);
    if(pollNetwork && context.session.roomScene.enabled && Network::hybrid_board_sync_enabled(context) &&
       player.timing.framePoll!=Runtime::TimePoint{} && pollNow-player.timing.framePoll>=pollInterval &&
       pollNow-player.timing.framePoll<pollInterval*2.)
        player.timing.framePoll+=pollInterval;
    else player.timing.framePoll=pollNow;
    return result;
}

} // namespace Engine
