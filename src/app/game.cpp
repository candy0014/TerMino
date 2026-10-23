#include "single_player_challenges.h"
#include "bot_battle_royale.h"
#include "online_replay.h"
#include "tetr/app/game.h"
#include "tetr/app/place.h"
#include "tetr/app/settings_menu.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/countdown_presentation.h"
#include "tetr/engine/input_precharge.h"
#include "tetr/input/services.h"
#include "tetr/engine/game_end_presentation.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/app/application_state.h"
#include "local_versus.h"
#include "multiplayer_battle.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/runtime/game_context.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/network/round_statistics.h"
#include "tetr/network/room_replay.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/engine/undo.h"
#include "tetr/engine/pause.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"

#include <stdexcept>
#include <cstdlib>
#include <iostream>

namespace App {

void run(Runtime::GameContext& context){
    if(context.replay_component() && Replay::session(context).multiplayerReplay) {
        run_multiplayer_replay(context);return;
    }
    if (context.config.mode.mode == Runtime::GameMode::LocalVersus) {
        if (!context.app_component() || !context.app_component()->localPlayerTwoContext) {
            throw std::logic_error("Local Versus requires a second player context");
        }
        run_local_versus(context, *context.app_component()->localPlayerTwoContext);
        return;
    }
    auto& first=context.session.player;
    auto& second=context.session.opponent;
    PieceLoopState pieceLoopState;
    pieceLoopState.reset();
    const bool roomBattle=context.session.roomScene.enabled;
    const bool roomParticipant=!roomBattle || wait_for_room_ready(context);
    // Pre-start observers share the visible countdown. A late observer can
    // enter the live boards directly, without starting a local player loop.
    if(roomBattle && !roomParticipant && Network::network_services(context).matchPhase!=Network::RoomMatchPhase::Countdown) {
        watch_room_match(context);return;
    }
    Input::GameplayInputScope inputScope(context.input_component(),roomParticipant);
    std::function<Runtime::Duration()> countdownRemaining;
    if(roomBattle && Network::network_services(context).countdownDeadline) {
        countdownRemaining=[&] {
            return Runtime::Duration::from_seconds(std::chrono::duration<double>(
                *Network::network_services(context).countdownDeadline-std::chrono::steady_clock::now()).count());
        };
    }
    if(!roomBattle && !context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer){
        Network::send_lobby_signal(context,Network::ClientLobbySignal::SetupReceived);
        Network::wait_for_lobby_signal(context,Network::ServerLobbySignal::CountdownReady);
    }

    const bool resumingReplay=context.config.mode.replayMode&&
        Replay::playback_resuming(context);
    if(context.config.mode.replayMode&&!resumingReplay)
        Engine::render_replay_playback_progress(context,first);
    auto countdown=resumingReplay
        ? Engine::CountdownResult::Completed
        : Engine::run_start_countdown(context,first,second,roomParticipant?&pieceLoopState.input:nullptr,[&] {
            if(roomBattle) {
                auto& n=Network::network_services(context);Network::poll_connection(n);
                if(n.connection!=Network::ConnectionState::Connected) throw Network::ConnectionError(n.connectionMessage);
                if(n.matchEpoch!=context.session.roomScene.epoch || n.matchCanceled)
                    throw Network::LobbyInterrupted("Match changed during countdown");
            }
            return Engine::refresh_game_terminal_layout(context);
        },[&](Engine::CountdownResult result) {
            return !roomBattle || Network::handle_room_game_control(context,result==Engine::CountdownResult::Retry);
        },countdownRemaining);
    if(roomBattle && countdown==Engine::CountdownResult::Completed && countdownRemaining) {
        // All clients and the relay already have the start deadline. No RTT
        // or completion acknowledgement belongs after the visible countdown.
        auto& n=Network::network_services(context);n.countdownStarted=true;n.matchPhase=Network::RoomMatchPhase::Playing;
    } else if(roomBattle && roomParticipant && countdown==Engine::CountdownResult::Completed)
        countdown=wait_for_room_round(context,pieceLoopState.input);
    if (countdown != Engine::CountdownResult::Completed && !context.config.mode.replayMode &&
        context.config.garbage.mode == Runtime::GarbageMode::Multiplayer)
        Network::send_lobby_signal(context, Network::ClientLobbySignal::Cancel);
    if(countdown==Engine::CountdownResult::Retry) {
        if(roomBattle) {inputScope.end();watch_room_match(context,true);}
        return;
    }
    const auto openSettings=[&] {
        if(auto* app=context.app_component();app && app->openSettings) app->openSettings();
        else Settings::run_menu(context);
    };
    if(countdown==Engine::CountdownResult::OpenSettings){ inputScope.end(); openSettings(); return; }
    if(roomBattle && !roomParticipant) {watch_room_match(context);return;}
    Engine::finish_start_precharge(context,pieceLoopState.input);

    if(!roomBattle && !context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer)
        Network::send_lobby_signal(context,Network::ClientLobbySignal::CountdownComplete);
    if (!roomBattle && !context.config.mode.replayMode &&
        context.config.garbage.mode == Runtime::GarbageMode::Multiplayer)
        Network::wait_for_lobby_signal(context, Network::ServerLobbySignal::RoundReady);

    begin_single_player_challenges(context);
    Engine::initialize_board_timers(context,first);
    if(context.config.mode.replayMode) Replay::start_playback(context,context.now());
    if(roomBattle) {
        for(auto& [slot,p]:context.session.roomScene.players) p.timing.realGameStart=first.timing.realGameStart;
    } else if(!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) Engine::initialize_board_timers(context,second);
    Engine::initialize_quick_play(context,first);
    Engine::GamePauseScope pauseScope(context,&pieceLoopState.input);
    context.undoHistory.reset();
    // Snapshots refer to this loop state only while this game is running.
    struct HistoryScope {
        Runtime::GameContext& context;
        ~HistoryScope() { context.undoHistory.reset(); }
    } historyScope{context};
    if(!context.config.mode.replayMode) {
        Replay::session(context).data=Replay::session(context).configPrefix;
        Replay::session(context).undoCount=0;
        Replay::session(context).pauseCount=0;
    }
    if(roomBattle && roomParticipant)Network::begin_room_replay_recording(context);
    struct OnlineRecordingScope {
        Runtime::GameContext& context;
        ~OnlineRecordingScope(){try{finish_online_replay(context);}catch(const std::exception& e){std::cerr<<"Replay finalization: "<<e.what()<<'\n';}}
    } onlineRecordingScope{context};

    auto botRoyale=start_bot_battle_royale(context);
    struct RoyaleScope {ApplicationState* app;~RoyaleScope(){if(app){app->challengeFrame={};app->challengeSendAttack={};}}} royaleScope{context.app_component()};
    Engine::GameLoopExit outcome;
    for (;;) {
        try {
            outcome=Engine::run_piece_loop(
                context,first,[&context,&pieceLoopState](Runtime::Playfield& field,Piece::Definition piece,Runtime::PlayerSessionState& player,bool holdContinuation){
                    return App::run_piece(context,field,piece,player,pieceLoopState,holdContinuation);
                }, &pieceLoopState);
            App::freeze_post_game_times(context,first);
            if (!context.config.mode.replayMode) context.undoHistory.reset();
            if(context.config.mode.replayMode){
                Engine::complete_replay_rebuild_at_game_end(context);
            }

            if(outcome==Engine::GameLoopExit::OpenSettings){
                pauseScope.end();
                if(roomBattle) {
                    finish_online_replay(context);
                    Network::send_round_statistics(context,first);
                    Network::handle_room_game_control(context,false);
                    first.presentation.activePose.reset();first.presentation.activePieceType.reset();
                    Network::send_board_state_frame(context,first,Network::ActivePieceState::locked(),
                        Network::network_services(context).boardSync.lastPlacement,true);
                    Network::send_round_signal(context,Network::OutgoingRoundSignal::LeaveRound);
                    Network::send_lobby_signal(context,Network::ClientLobbySignal::Cancel);
                }
                else if(!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) Engine::close_multiplayer_round(context);
                inputScope.end(); openSettings(); return;
            }
            if(outcome==Engine::GameLoopExit::Retry){
                if(roomBattle) {
                    finish_online_replay(context);
                    Network::send_round_statistics(context,first);
                    Network::handle_room_game_control(context,true);
                    pauseScope.end();inputScope.end();watch_room_match(context,true);
                }
                else if(!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) Engine::close_multiplayer_round(context);
                return;
            }
            if(roomBattle) {
                if(outcome==Engine::GameLoopExit::Won) ++first.gameplay.scoring.wins;
                finish_online_replay(context);
                Network::send_round_statistics(context,first);
                if(outcome!=Engine::GameLoopExit::Won) {
                    // Send the final fixed field (including fatal garbage)
                    // before elimination freezes the relay's cached board.
                    first.presentation.activePose.reset();first.presentation.activePieceType.reset();
                    Network::send_board_state_frame(context,first,Network::ActivePieceState::locked(),
                        Network::network_services(context).boardSync.lastPlacement,true);
                    Network::send_round_signal(context,Network::OutgoingRoundSignal::LocalLoss);
                }
                pauseScope.end();inputScope.end();
                // Repeat the loss until the authoritative roster marks this slot eliminated.
                auto& n=Network::network_services(context);
                for(int attempt=0;attempt<20 && context.session.roomScene.selfAlive && n.matchPhase==Network::RoomMatchPhase::Playing;++attempt) {
                    Network::send_round_signal(context,Network::OutgoingRoundSignal::LocalLoss);
                    Engine::poll_multiplayer_scene(context,false);context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.05));
                }
                watch_room_match(context,outcome!=Engine::GameLoopExit::Won);return;
            }
            if(context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) Engine::close_multiplayer_round(context);
            const Runtime::Duration finalElapsed=*first.timing.completedGameTime;
            if (std::getenv("TETR_TEST_METRICS") && !context.config.mode.replayMode) {
                const auto& score = first.gameplay.scoring;
                std::cerr << "TETR_TEST_RESULT"
                          << " side=first"
                          << " outcome=" << (outcome == Engine::GameLoopExit::Dead ? "dead" :
                                outcome == Engine::GameLoopExit::GoalReached ? "goal" :
                                outcome == Engine::GameLoopExit::Won ? "won" : "other")
                          << " duration=" << finalElapsed.seconds()
                          << " pieces=" << score.placedBlocks
                          << " attack=" << score.totalAttack
                          << " outgoing=" << score.totalOutgoingAttack
                          << " lines=" << score.totalLines
                          << " garbage_cleared=" << score.totalGarbageLines
                          << " b2b=" << score.b2b << '\n';
            }
            Engine::render_final_mode_result(context,first,finalElapsed);
            if (context.config.mode.replayMode) {
                Replay::finish_playback(context,context.now());
                Engine::render_replay_playback_progress(context,first);
            }
            pauseScope.end();
            if (!context.config.mode.replayMode) inputScope.end();
            App::run_post_game_interaction(context,first);
            return;
        } catch (const Engine::UndoRequested&) {
            if (!Engine::restore_previous_undo_snapshot(context)) return;
            inputScope.restart();
            Render::OutputGuard output(context);
            if (context.config.mode.replayMode) Render::end_output_suppression();
            Engine::synchronize_field_visibility(context,first);
            if (const auto size=Render::terminal_size()) Engine::center_game_boards(context,*size);
            Render::clear_screen();
            Render::redraw_player_board(context,first);
            Engine::GarbageService::redraw_buffer(context,first);
            Engine::render_replay_playback_progress(context,first);
            Render::flush();
        } catch (const Engine::ReplaySeekRequested&) {
            return;
        }
    }
}


} // namespace App
