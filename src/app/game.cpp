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
#include "tetr/runtime/game_context.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/replay/session_state.h"
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
    Input::GameplayInputScope inputScope(context.input_component());

    if(!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer){
        Network::send_lobby_signal(context,Network::ClientLobbySignal::SetupReceived);
        Network::wait_for_lobby_signal(context,Network::ServerLobbySignal::CountdownReady);
    }

    const bool resumingReplay=context.config.mode.replayMode&&
        Replay::playback_resuming(context);
    if(context.config.mode.replayMode&&!resumingReplay)
        Engine::render_replay_playback_progress(context,first);
    const auto countdown=resumingReplay
        ? Engine::CountdownResult::Completed
        : Engine::run_start_countdown(context,first,second,&pieceLoopState.input,[&] {
            return Engine::refresh_game_terminal_layout(context);
        });
    if (countdown != Engine::CountdownResult::Completed && !context.config.mode.replayMode &&
        context.config.garbage.mode == Runtime::GarbageMode::Multiplayer)
        Network::send_lobby_signal(context, Network::ClientLobbySignal::Cancel);
    if(countdown==Engine::CountdownResult::Retry) return;
    if(countdown==Engine::CountdownResult::OpenSettings){ inputScope.end(); Settings::run_menu(context); return; }
    Engine::finish_start_precharge(context,pieceLoopState.input);

    if(!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer)
        Network::send_lobby_signal(context,Network::ClientLobbySignal::CountdownComplete);
    if (!context.config.mode.replayMode &&
        context.config.garbage.mode == Runtime::GarbageMode::Multiplayer)
        Network::wait_for_lobby_signal(context, Network::ServerLobbySignal::RoundReady);

    Engine::initialize_board_timers(context,first);
    if(context.config.mode.replayMode) Replay::start_playback(context,context.now());
    if(!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) Engine::initialize_board_timers(context,second);
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
                if(!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) Engine::close_multiplayer_round(context);
                inputScope.end(); Settings::run_menu(context); return;
            }
            if(outcome==Engine::GameLoopExit::Retry){
                if(!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) Engine::close_multiplayer_round(context);
                return;
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
            Engine::synchronize_qp_invisible_flash(context,first);
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
