#include "tetr/engine/game_loop_adapter.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/piece.h"
#include "tetr/engine/piece_queue.h"
#include "next_preview_presentation.h"
#include "tetr/runtime/game_context.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/rules/game_completion.h"
#include "tetr/rules/game_result.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/playback_checkpoint.h"
#include "tetr/engine/simulation_time.h"
#include "tetr/engine/replay_application.h"
#include "tetr/engine/undo.h"
#include "tetr/app/application_state.h"
#include "tetr/input/services.h"

#include <iostream>
#include <cstdlib>
#include <algorithm>

namespace {
int test_limit(const char* key) {
    const char* value = std::getenv(key);
    if (!value || !*value) return 0;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    return end != value && *end == '\0' && parsed > 0 && parsed <= 1000000
        ? static_cast<int>(parsed) : 0;
}
double test_seconds_limit() {
    const char* value = std::getenv("TETR_TEST_MAX_SECONDS");
    if (!value || !*value) return 0.0;
    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    return end != value && *end == '\0' && parsed > 0.0 ? parsed : 0.0;
}
}

namespace Engine {
static bool racing_goal_reached(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player) {
    if(context.config.mode.mode!=Runtime::GameMode::Racing) return false;
    if(context.config.initialMap && context.config.mode.mapGoal!=Runtime::MapGoal::Disabled)
        return Rules::racing_map_goal_reached(context.config.mode.mapGoal,player.gameplay.playfield.field.remaining_initial_cells(),player.gameplay.scoring.mapAllClearReached);
    const auto& scoring=player.gameplay.scoring;
    return Rules::racing_goal_reached(context.config.mode.mode,context.config.garbage.mode,scoring.totalLines,scoring.totalGarbageLines,context.config.mode.racingDistance);
}

static bool should_show_multiplayer_round_message(
    const Runtime::GameContext& context) {
    return context.config.mode.mode != Runtime::GameMode::LocalVersus;
}

static void show_multiplayer_round_message(Runtime::GameContext& context,Runtime::PlayerSessionState& player,const char* headline){
    player.presentation.roundEndHeadline=headline;
    Render::OutputGuard output(context);
    Render::set_ui_color(context.config.display.obscuredColors ? Render::ColorRole::Ghost : Render::ColorRole::Text,context.config.display.obscuredColors);
    Render::move_cursor(1,player.presentation.margin);
    std::cout<<headline;
    Render::move_cursor(2,player.presentation.margin);
    std::cout<<"Press \"Retry\" to wait for a new game.";
    Render::move_cursor(3,player.presentation.margin);
    std::cout<<"Press \"Settings\" to open settings.";
    fflush(stdout);
}

GameLoopExit run_piece_loop(Runtime::GameContext& context,Runtime::PlayerSessionState& player,const PlayPiece& playPiece,
    PieceLoopState* loopState){
    Input::GameplayInputScope inputScope(context.input_component());
    if (!context.undoHistory) begin_undo_history(context,loopState);
    int pieceLimit = test_limit("TETR_TEST_MAX_PIECES");
    // Benchmarks may cap only their subject; a faster opponent must not end
    // the match merely by reaching the subject's piece limit first.
    if (std::getenv("TETR_TEST_MAX_PIECES_FIRST_ONLY") && context.app_component() &&
        context.app_component()->primaryContext != &context) pieceLimit = 0;
    const bool unlimitedMatch = context.app_component() && context.app_component()->automaticLocalMatch;
    if (unlimitedMatch) pieceLimit = 0;
    const int garbageLimit = unlimitedMatch ? 0 : test_limit("TETR_TEST_MAX_GARBAGE_CLEARED");
    const double secondsLimit = unlimitedMatch ? 0 : test_seconds_limit();
    auto& gameplay=player.gameplay;
    auto& playfield=gameplay.playfield;
    auto& scoring=gameplay.scoring;
    auto& hold=gameplay.hold;
    bool holdContinuation=false;
    for(;;){
        poll_undo_key(context);
        if(racing_goal_reached(context,player)) return GameLoopExit::GoalReached;
        if(finite_map_sequence(context) && player.pieces.bag.empty()) {
            if(!hold.piece.valid()) {
                if(!context.config.mode.replayMode && context.config.garbage.mode==Runtime::GarbageMode::Multiplayer)
                    Network::send_round_signal(context,Network::OutgoingRoundSignal::LeaveRound);
                return context.config.mode.mode==Runtime::GameMode::Racing ? GameLoopExit::Dead : GameLoopExit::GoalReached;
            }
            Render::draw_board_hold(context,player,Render::PieceDrawMode::Erase);
            prepend_piece(player,hold.piece);
            hold.piece=Piece::Type::none();hold.used=0;holdContinuation=false;
        }
        if (!holdContinuation) {
            hold.used=0;
            const auto turn = context.config.mode.replayMode
                ? wait_for_replay_start(context, player) : wait_for_piece_turn(context, player);
            if (turn == Rules::PlayResult::Dead) return GameLoopExit::Dead;
            if (turn == Rules::PlayResult::Won) return GameLoopExit::Won;
            if (turn == Rules::PlayResult::OpponentLeft) return GameLoopExit::OpponentLeft;
            if (turn == Rules::PlayResult::OpenSettings) return GameLoopExit::OpenSettings;
            if (turn == Rules::PlayResult::Retry) return GameLoopExit::Retry;
        }
        if(context.config.mode.replayMode&&!holdContinuation){
            Replay::capture_playback_checkpoint(context);
        }
        prepare_next_preview(context,player);
        if(!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer)
            Network::send_board_state_frame(context,player);

        const Piece::Type currentPiece=Engine::current_piece(player);
        const Rules::PlayResult result=playPiece(playfield.field,Piece::get_definition(currentPiece),player,holdContinuation);
        Engine::consume_current_piece(player);

        if(result==Rules::PlayResult::Dead){
            capture_undo_snapshot(context);
            if(!context.config.mode.replayMode&&context.config.garbage.mode == Runtime::GarbageMode::Multiplayer&&should_show_multiplayer_round_message(context)) show_multiplayer_round_message(context,player,"You lost this game!");
            return GameLoopExit::Dead;
        }
        if(result==Rules::PlayResult::OpenSettings) return GameLoopExit::OpenSettings;
        if(result==Rules::PlayResult::OpponentLeft){ if(should_show_multiplayer_round_message(context)) show_multiplayer_round_message(context,player,"Your opponent has left the game!"); return GameLoopExit::OpponentLeft; }
        if(result==Rules::PlayResult::Won){ if(should_show_multiplayer_round_message(context)) show_multiplayer_round_message(context,player,"You won this game!"); return GameLoopExit::Won; }
        if(result==Rules::PlayResult::Retry) return GameLoopExit::Retry;

        if(result==Rules::PlayResult::Hold){
            ++scoring.totalHolds;
            hold.used=std::min(hold.used,1000000000-1)+1;
            if(hold.piece.valid()){ Engine::prepend_piece(player,hold.piece); hold.piece=currentPiece; holdContinuation=true; }
            else { hold.piece=currentPiece; holdContinuation=true; }
        }
        if(result==Rules::PlayResult::Locked){
            hold.used=0;
            if(holdContinuation) Render::draw_board_hold(context,player);
            holdContinuation=false;
            capture_undo_snapshot(context);
        }

        if(racing_goal_reached(context,player))
            return GameLoopExit::GoalReached;
        const Runtime::Duration elapsed=context.config.mode.replayMode
            ? Replay::playback_elapsed(context,context.now())
            : Runtime::elapsed_game_time(context.session,context.now());
        if(!finite_map_sequence(context) && Rules::blitz_goal_reached(context.config.mode.mode,elapsed,context.config.mode.blitzTime))
            return GameLoopExit::GoalReached;
        if (!context.config.mode.replayMode &&
            ((pieceLimit > 0 && scoring.placedBlocks >= pieceLimit) ||
             (garbageLimit > 0 && scoring.totalGarbageLines >= garbageLimit) ||
             (secondsLimit > 0.0 && elapsed.seconds() >= secondsLimit))) {
            if (context.config.garbage.mode == Runtime::GarbageMode::Multiplayer)
                Network::send_round_signal(context, Network::OutgoingRoundSignal::LeaveRound);
            return GameLoopExit::GoalReached;
        }
    }
}

void close_multiplayer_round(Runtime::GameContext& context){
    if(context.config.garbage.mode != Runtime::GarbageMode::Multiplayer) return;
    Network::acknowledge_round_complete(context);
}

} // namespace Engine
