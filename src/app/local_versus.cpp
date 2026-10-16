#include "local_versus.h"
#include "single_player_challenges.h"

#include "tetr/app/application_state.h"
#include "tetr/app/place.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/app/settings_menu.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/countdown_presentation.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/input_precharge.h"
#include "tetr/input/services.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/turn_timeline.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace App {
namespace {

Engine::GameLoopExit run_side(Runtime::GameContext& context, PieceLoopState& loopState) {
    struct Finish {
        Runtime::GameContext& context;
        ~Finish() { Replay::finish_turn_playback(context); }
    } finish{context};
    try {
        auto& player = context.session.player;
        Engine::initialize_quick_play(context, player);
        if (!context.config.mode.replayMode) {
            Replay::session(context).data = Replay::session(context).configPrefix;
            Replay::session(context).undoCount = 0;
            Replay::session(context).pauseCount = 0;
        }
        const auto outcome = Engine::run_piece_loop(
            context,
            player,
            [&context, &loopState](Runtime::Playfield& field,
                                  Piece::Definition piece,
                                  Runtime::PlayerSessionState& current,
                                  bool holdContinuation) {
                return App::run_piece(
                    context, field, piece, current, loopState, holdContinuation);
            },&loopState);
        freeze_post_game_times(context,player);
        if (context.config.mode.replayMode)
            Engine::update_replay_playback_controls(context, player, true);
        return outcome;
    } catch (const Engine::ReplaySeekRequested&) {
        return Engine::GameLoopExit::Retry;
    } catch (const StartScreenRequested&) {
        return Engine::GameLoopExit::Retry;
    }
}

const char* outcome_name(Engine::GameLoopExit outcome) {
    switch (outcome) {
        case Engine::GameLoopExit::Dead: return "dead";
        case Engine::GameLoopExit::Won: return "won";
        case Engine::GameLoopExit::GoalReached: return "goal";
        case Engine::GameLoopExit::OpponentLeft: return "opponent_left";
        case Engine::GameLoopExit::OpenSettings: return "settings";
        case Engine::GameLoopExit::Retry: return "retry";
    }
    return "unknown";
}

void print_batch_summary(
    const Runtime::GameContext& first,
    const Runtime::GameContext& second,
    const std::array<Engine::GameLoopExit, 2>& outcomes) {
    if (!std::getenv("TETR_LOCAL_VERSUS_SUMMARY")) return;
    const auto& a = first.session.player.gameplay.scoring;
    const auto& b = second.session.player.gameplay.scoring;
    std::cerr
        << "TETR_LOCAL_VERSUS_RESULT"
        << " first_outcome=" << outcome_name(outcomes[0])
        << " second_outcome=" << outcome_name(outcomes[1])
        << " first_pieces=" << a.placedBlocks
        << " second_pieces=" << b.placedBlocks
        << " first_attack=" << a.totalAttack
        << " second_attack=" << b.totalAttack
        << " first_lines=" << a.totalLines
        << " second_lines=" << b.totalLines
        << " first_garbage_cleared=" << a.totalGarbageLines
        << " second_garbage_cleared=" << b.totalGarbageLines
        << " first_b2b=" << a.b2b
        << " second_b2b=" << b.b2b
        << " first_combo=" << a.combo
        << " second_combo=" << b.combo
        << " first_difficult_clears=" << a.difficultClears
        << " second_difficult_clears=" << b.difficultClears
        << " first_b2b_extensions=" << a.b2bExtensions
        << " second_b2b_extensions=" << b.b2bExtensions
        << " first_b2b_breaks=" << a.b2bBreaks
        << " second_b2b_breaks=" << b.b2bBreaks
        << " first_max_b2b=" << a.maxB2B
        << " second_max_b2b=" << b.maxB2B
        << " first_max_combo=" << a.maxCombo
        << " second_max_combo=" << b.maxCombo
        << " first_clear_moves=" << a.clearMoves
        << " second_clear_moves=" << b.clearMoves
        << " first_zero_attack_clears=" << a.zeroAttackClears
        << " second_zero_attack_clears=" << b.zeroAttackClears
        << " first_attacks_ge4=" << a.attacksAtLeast4
        << " second_attacks_ge4=" << b.attacksAtLeast4
        << " first_attacks_ge8=" << a.attacksAtLeast8
        << " second_attacks_ge8=" << b.attacksAtLeast8
        << " first_max_attack=" << a.maxAttack
        << " second_max_attack=" << b.maxAttack
        << " first_outgoing=" << a.totalOutgoingAttack
        << " second_outgoing=" << b.totalOutgoingAttack
        << '\n';
}

} // namespace

void run_local_versus(Runtime::GameContext& first, Runtime::GameContext& second) {
    auto& firstPlayer = first.session.player;
    auto& secondPlayer = second.session.player;
    PieceLoopState firstLoop, secondLoop;
    firstLoop.reset(); secondLoop.reset();
    Input::GameplayInputScope firstInput(first.input_component());
    Input::GameplayInputScope secondInput(second.input_component());

    const bool resumingReplay=first.config.mode.replayMode&&
        Replay::playback_resuming(first);
    if (first.config.mode.replayMode&&!resumingReplay) {
        Engine::render_replay_playback_progress(first,firstPlayer);
    }
    const auto countdown = resumingReplay
        ? Engine::CountdownResult::Completed
        : Engine::run_local_versus_countdown(
            first, firstPlayer, second, secondPlayer,&firstLoop.input,&secondLoop.input,[&] {
                const bool a=Engine::refresh_game_terminal_layout(first);
                const bool b=Engine::refresh_game_terminal_layout(second);
                return a || b;
            });
    if (countdown == Engine::CountdownResult::Retry) return;
    if (countdown == Engine::CountdownResult::OpenSettings) {
        firstInput.end(); secondInput.end();
        Settings::run_menu(first);
        return;
    }

    Engine::finish_start_precharge(first,firstLoop.input);
    Engine::finish_start_precharge(second,secondLoop.input);

    const int firstWinsBefore = firstPlayer.gameplay.scoring.wins;
    const int secondWinsBefore = secondPlayer.gameplay.scoring.wins;
    begin_single_player_challenges(first);begin_single_player_challenges(second);
    std::array<Engine::GameLoopExit, 2> outcomes{
        Engine::GameLoopExit::Retry,
        Engine::GameLoopExit::Retry};

    // Establish both origins before launching either side, so recordings and
    // replay scheduling share the same physical timeline.
    Engine::initialize_board_timers(first, firstPlayer);
    Engine::initialize_board_timers(second, secondPlayer);
    if (first.config.mode.replayMode) {
        Replay::start_playback(first, first.now());
        Replay::start_playback(second, second.now());
    } else if (first.config.turnPieces > 0 && second.config.turnPieces > 0) {
        const auto origin = first.real_now();
        Replay::session(first).recordingStart = origin;
        Replay::session(second).recordingStart = origin;
    }

    std::thread firstThread([&] { outcomes[0] = run_side(first,firstLoop); });
    std::thread secondThread([&] { outcomes[1] = run_side(second,secondLoop); });
    firstThread.join();
    secondThread.join();
    firstInput.end(); secondInput.end();

    if (first.app_component() && first.app_component()->startScreenRequested) {
        firstPlayer.gameplay.scoring.wins = firstWinsBefore;
        secondPlayer.gameplay.scoring.wins = secondWinsBefore;
        throw StartScreenRequested{};
    }

    if (first.app_component()&&
        first.app_component()->replayResume.pending) {
        return;
    }
    if(first.config.mode.replayMode){
        Engine::complete_replay_rebuild_at_game_end(first);
    }

    if (!first.config.mode.replayMode) {
        Engine::close_multiplayer_round(first);
        Engine::close_multiplayer_round(second);
    }

    const bool openingSettings =
        outcomes[0] == Engine::GameLoopExit::OpenSettings ||
        outcomes[1] == Engine::GameLoopExit::OpenSettings;
    if (openingSettings) {
        // Leaving a round through Settings is configuration, not a scored win.
        firstPlayer.gameplay.scoring.wins = firstWinsBefore;
        secondPlayer.gameplay.scoring.wins = secondWinsBefore;
        Settings::run_menu(first);
        return;
    }
    if (outcomes[0] == Engine::GameLoopExit::Retry ||
        outcomes[1] == Engine::GameLoopExit::Retry) {
        return;
    }

    print_batch_summary(first, second, outcomes);
    if(!first.config.mode.replayMode) {
        finish_single_player_challenges(first,firstPlayer,outcomes[0]==Engine::GameLoopExit::Won);
        finish_single_player_challenges(second,secondPlayer,outcomes[1]==Engine::GameLoopExit::Won);
    }
    if (std::getenv("TETR_TEST_METRICS") && !first.config.mode.replayMode) {
        std::cerr << "TETR_TEST_DURATION"
                  << " first=" << Runtime::elapsed_game_time(first.session, first.now()).seconds()
                  << " second=" << Runtime::elapsed_game_time(second.session, second.now()).seconds()
                  << " first_wins=" << firstPlayer.gameplay.scoring.wins
                  << " second_wins=" << secondPlayer.gameplay.scoring.wins
                  << '\n';
    }
    if (first.config.mode.replayMode) {
        Replay::finish_playback(first,first.now());
        Replay::finish_playback(second,second.now());
        Engine::render_replay_playback_progress(first,firstPlayer);
    }
    try {
        App::run_local_versus_post_game_interaction(first, second);
    } catch (const Engine::ReplaySeekRequested&) {
        return;
    }
}

} // namespace App
