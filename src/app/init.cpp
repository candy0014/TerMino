#include "tetr/input/services.h"
#include "tetr/app/init.h"
#include "tetr/app/settings_menu.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/app/application_state.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/tetrio_recorder.h"
#include "tetr/replay/playback_checkpoint.h"

#include "init_multiplayer.h"
#include "init_player_setup.h"
#include "init_replay.h"
#include "game_terminal.h"
#include "single_player_challenges.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/constants.h"
#include "tetr/config/normalization.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/config/user_config.h"
#include "tetr/network/network_services.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/multiplayer_channel.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace App {
namespace {

std::optional<std::uint32_t> local_versus_seed_override() {
    const char* text = std::getenv("TETR_LOCAL_VERSUS_SEED");
    if (!text || *text == '\0') return std::nullopt;

    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}


void seed_player(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    bool replayMode,
    bool multiplayer) {
    player.pieces.tetrioRandom.reset();
    player.garbage.tetrioRandom.reset();
    if (replayMode) return;

    std::uint32_t blockSeed = context.services.random();
    std::uint32_t garbageSeed = context.services.random();
    if (multiplayer) {
        garbageSeed = InitDetail::multiplayer_seed(context);
        blockSeed = garbageSeed ^ Runtime::kMultiplayerBlockSeedSalt;
    } else if (const char* text = std::getenv("TETR_TEST_SEED")) {
        char* end = nullptr;
        const unsigned long value = std::strtoul(text, &end, 10);
        if (end != text && *end == '\0' && value <= std::numeric_limits<std::uint32_t>::max()) {
            blockSeed = static_cast<std::uint32_t>(value);
            garbageSeed = blockSeed ^ Runtime::kMultiplayerBlockSeedSalt;
        }
    }
    player.pieces.random.seed(blockSeed);
    player.garbage.random.seed(garbageSeed);
    player.attackRoundingRandom.seed(
        garbageSeed ^ Runtime::kAttackRoundingSeedSalt);
    Replay::session(context).configPrefix += "{block_seed," + std::to_string(blockSeed) + "}";
    Replay::session(context).configPrefix += "{garbage_seed," + std::to_string(garbageSeed) + "}";
    Replay::session(context).configPrefix += "}";
}

void seed_player_with_values(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    std::uint32_t blockSeed,
    std::uint32_t garbageSeed) {
    if (const auto* recorder = Replay::tetrio_recorder(context)) {
        blockSeed = garbageSeed = recorder->seed();
        player.pieces.tetrioRandom.emplace(blockSeed);
        player.garbage.tetrioRandom.emplace(garbageSeed);
    } else {
        player.pieces.tetrioRandom.reset();
        player.garbage.tetrioRandom.reset();
    }
    player.pieces.random.seed(blockSeed);
    player.garbage.random.seed(garbageSeed);
    player.attackRoundingRandom.seed(
        garbageSeed ^ Runtime::kAttackRoundingSeedSalt);
    Replay::session(context).configPrefix += "{block_seed," + std::to_string(blockSeed) + "}";
    Replay::session(context).configPrefix += "{garbage_seed," + std::to_string(garbageSeed) + "}";
    Replay::session(context).configPrefix += "}";
}

void initialize_presentation(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::PlayerSessionState& opponent,
    bool multiplayer) {
    // Room preparation retains the waiting screen. The ready/countdown
    // transition owns the first game-board draw, including late spectators.
    if(context.session.roomScene.enabled)return;
    Render::clear_screen();
    Render::set_ui_color(
        context.config.display.obscuredColors ? Render::ColorRole::Ghost : Render::ColorRole::Text,
        context.config.display.obscuredColors);

    InitDetail::draw_initial_board(context, player);
    if (multiplayer) InitDetail::draw_initial_board(context, opponent);

    if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay) {
        Render::print_altitude(context, player);
    } else {
        Render::print_score(context, player);
        if (multiplayer) Render::print_score(context, opponent);
    }
    Render::print_message(context, player);
    if (multiplayer) Render::print_message(context, opponent);
}

void apply_initial_garbage(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    if(context.config.initialMap) return;
    if (context.config.garbage.mode != Runtime::GarbageMode::CheeseLayer) return;
    Engine::GarbageService::add_garbage(
        context,
        player,
        player.gameplay.playfield.field,
        context.config.garbage.layerHeight,
        true);
}

Runtime::GameContext* local_player_two_context(Runtime::GameContext& context) {
    if (!context.app_component()) return nullptr;
    return context.app_component()->localPlayerTwoContext;
}

bool initialize_local_versus_presentation(
    Runtime::GameContext& firstContext,
    Runtime::GameContext& secondContext) {
    auto& first = firstContext.session.player;
    auto& second = secondContext.session.player;
    first.presentation.margin = 30;
    second.presentation.margin = first.presentation.margin + std::max(
        40,
        (first.gameplay.playfield.width + first.presentation.blockMaxWidth * 2) * 2 + 20);

    if (InitDetail::wait_for_game_terminal(firstContext,&secondContext)==
        InitDetail::TerminalCheckResult::OpenSettings) return false;

    Render::clear_screen();
    Render::set_ui_color(
        firstContext.config.display.obscuredColors
            ? Render::ColorRole::Ghost
            : Render::ColorRole::Text,
        firstContext.config.display.obscuredColors);
    InitDetail::draw_initial_board(firstContext, first);
    Render::print_score(firstContext, first);
    Render::print_message(firstContext, first);

    Render::set_ui_color(
        secondContext.config.display.obscuredColors
            ? Render::ColorRole::Ghost
            : Render::ColorRole::Text,
        secondContext.config.display.obscuredColors);
    InitDetail::draw_initial_board(secondContext, second);
    Render::print_score(secondContext, second);
    Render::print_message(secondContext, second);
    return true;
}

bool initialize_local_versus(Runtime::GameContext& firstContext, Runtime::GameContext& secondContext) {
    Input::refresh_bindings(
        firstContext.input_services(), firstContext.user_settings().keyCodes, firstContext.user_settings().additionalKeyCodes);
    Input::refresh_bindings(
        secondContext.input_services(), secondContext.user_settings().keyCodes, secondContext.user_settings().additionalKeyCodes);

    Runtime::GameMode localMode = Runtime::GameMode::LocalVersus;
    InitDetail::configure_replay_or_local_game(firstContext, localMode);
    InitDetail::configure_replay_or_local_game(secondContext, localMode);
    Runtime::reset_dynamic_rules(firstContext);
    Runtime::reset_dynamic_rules(secondContext);

    Network::connect_local_duplex(
        *firstContext.network_component(), *secondContext.network_component());

    auto& first = firstContext.session.player;
    auto& second = secondContext.session.player;
    InitDetail::reset_player_board(first, firstContext.config);
    InitDetail::reset_player_board(second, secondContext.config);
    // Presentation is drawn before the countdown.  Clear the previous round's
    // start timestamps first so both clocks visibly start from 0.00.
    firstContext.session.gameStartTime.reset();
    secondContext.session.gameStartTime.reset();

    const auto seedOverride = local_versus_seed_override();
    const std::uint32_t garbageSeed = Replay::tetrio_recorder(firstContext)
        ? Replay::tetrio_recorder(firstContext)->seed() : seedOverride
        ? *seedOverride
        : firstContext.services.random();
    const std::uint32_t blockSeed = garbageSeed ^ Runtime::kMultiplayerBlockSeedSalt;
    Network::configure_piece_turns(firstContext, secondContext.config.turnPieces, 0, garbageSeed);
    Network::configure_piece_turns(secondContext, firstContext.config.turnPieces, 1, garbageSeed);
    seed_player_with_values(firstContext, first, blockSeed, garbageSeed);
    seed_player_with_values(secondContext, second, blockSeed, garbageSeed);
    InitDetail::initialize_player_field(firstContext, first);
    InitDetail::initialize_player_field(secondContext, second);

    return initialize_local_versus_presentation(firstContext, secondContext);

}

bool initialize_local_versus_replay(
    Runtime::GameContext& firstContext,
    Runtime::GameContext& secondContext) {
    Input::refresh_bindings(
        secondContext.input_services(), secondContext.user_settings().keyCodes, secondContext.user_settings().additionalKeyCodes);
    Runtime::reset_dynamic_rules(firstContext);
    Runtime::reset_dynamic_rules(secondContext);

    auto& first = firstContext.session.player;
    auto& second = secondContext.session.player;
    InitDetail::reset_player_board(first, firstContext.config);
    InitDetail::reset_player_board(second, secondContext.config);
    firstContext.session.gameStartTime.reset();
    secondContext.session.gameStartTime.reset();
    InitDetail::initialize_player_field(firstContext, first);
    InitDetail::initialize_player_field(secondContext, second);

    const auto progressBottom=[](Runtime::GameContext& owner,const Runtime::PlayerSessionState& player) {
        return Render::board_vertical_extent(owner,player,false).footer+
            (owner.config.garbage.mode==Runtime::GarbageMode::QuickPlay ? 4 : 2);
    };
    const int progressRow=std::max(progressBottom(firstContext,first),progressBottom(secondContext,second));
    Replay::session(firstContext).playback.progressRow=progressRow;
    Replay::session(secondContext).playback.progressRow=progressRow;
    const Runtime::Duration sharedDuration=std::max(
        Replay::session(firstContext).playback.duration,
        Replay::session(secondContext).playback.duration);
    Replay::session(firstContext).playback.duration=sharedDuration;
    Replay::session(secondContext).playback.duration=sharedDuration;
    Replay::session(firstContext).playback.progressVisible=true;
    Replay::session(secondContext).playback.progressVisible=false;

    if (!initialize_local_versus_presentation(firstContext,secondContext)) return false;
    Replay::restore_scheduled_playback_checkpoint(firstContext);
    Replay::restore_scheduled_playback_checkpoint(secondContext);
    return true;

}

void configure_after_terminal_check(Runtime::GameContext& context, Runtime::GameContext* secondContext) {
    // Size confirmation runs before Prepare, so opening settings here must
    // not cancel another player's existing preparation.
    if (Settings::run_menu(context)==Settings::MenuResult::StartScreen) request_start_screen(context);
    // The menu's restart action must be released before a fresh game loop.
    const auto held=[](Runtime::GameContext& target) {
        for (const auto& [key,action]:target.input_services().bindings.entries())
            if (action==Input::Action::Retry && target.input_services().key_down(key)) return true;
        return false;
    };
    while (held(context) || (secondContext && held(*secondContext)))
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
}

} // namespace

void initialize_game(Runtime::GameContext& context) {
    if(auto* app=context.app_component();app && app->replayPreview) {
        if(app->replayPreview->pending)app->replayPreview->pending=false;
        else if(!app->replayResume.pending)app->replayPreview.reset();
    }
    if(auto* app=context.app_component()){
        app->challengeRound.reset();app->challengePlacementObserver={};
        app->challengeFrame={};app->challengeSendAttack={};
        app->playerTwoChallengeRound.reset();app->playerTwoChallengePlacementObserver={};
    }
    if(context.user_settings().challengeReplay) {
        auto& restore=*context.user_settings().challengeReplay;
        if(restore.pending)restore.pending=false;
        else if(!context.app_component() || !context.app_component()->replayResume.pending)restore_challenge_replay(context);
    }
    Settings::synchronize_shared_appearance(context);
    for (;;) {
        context.session.roomScene={};
        context.pause = {};
        context.session.simulationTime = {};
        context.session.completedSimulationPieces = 0;
        context.session.turnWaitStarted.reset();
        context.session.turnWaitDuration = {};
        if (context.network_component()) context.network_component()->pieceTurns = {};
        Input::refresh_bindings(context.input_services(), context.user_settings().keyCodes, context.user_settings().additionalKeyCodes);

        Runtime::GameMode selectedMode = Runtime::selected_game_mode(context.user_settings());
        if(auto* app=context.app_component();app && app->replayPreview)selectedMode=Runtime::GameMode::Replay;
        if(context.network_component() && context.network_component()->initialized)
            Network::synchronize_room_player_mode(*context.network_component(),selectedMode==Runtime::GameMode::Multiplayer,
                context.user_settings().gameMode);
        Runtime::GameContext* secondContext = local_player_two_context(context);
        if (selectedMode == Runtime::GameMode::LocalVersus) {
            if (!secondContext) {
                throw std::logic_error("Local Versus requires a second player context");
            }
            if (initialize_local_versus(context, *secondContext)) return;
            configure_after_terminal_check(context,secondContext);
            continue;
        }
        if (context.network_component()) Network::disconnect_local_duplex(*context.network_component());
        if (secondContext && secondContext->network_component()) {
            Network::disconnect_local_duplex(*secondContext->network_component());
        }
        const bool replayMode = InitDetail::configure_replay_or_local_game(
            context, selectedMode, secondContext);
        if(!replayMode && context.user_settings().challengeReplay) {
            restore_challenge_replay(context);continue;
        }
        if (replayMode && Replay::session(context).localVersusReplay) {
            if (!secondContext) {
                throw std::logic_error("Local Versus replay requires a second player context");
            }
            if (initialize_local_versus_replay(context, *secondContext)) return;
            configure_after_terminal_check(context,secondContext);
            continue;
        }
        Runtime::reset_dynamic_rules(context);
        const bool multiplayer =
            !replayMode && context.config.garbage.mode == Runtime::GarbageMode::Multiplayer;

        if (multiplayer) {
            auto& network = Network::network_services(context);
            if (!network.initialized || network.serverIp != context.user_settings().serverIp ||
                (network.connection != Network::ConnectionState::Connected &&
                 network.connection != Network::ConnectionState::Connecting)) {
                Network::initialize_network_services(
                    context, context.user_settings().serverIp);
            }
            // Confirm the viewport before entering match preparation. Once in
            // a room, later resizes only update the game layout.
            InitDetail::reset_player_board(context.session.player,context.config);
            InitDetail::initialize_player_field(context,context.session.player);
            context.session.opponent.gameplay.playfield=context.session.player.gameplay.playfield;
            context.session.opponent.presentation.blockMaxWidth=context.session.player.presentation.blockMaxWidth;
            context.session.opponent.presentation.blockMaxHeight=context.session.player.presentation.blockMaxHeight;
            if(InitDetail::wait_for_game_terminal(context)==InitDetail::TerminalCheckResult::OpenSettings) {
                configure_after_terminal_check(context,nullptr);continue;
            }
            if (InitDetail::wait_for_multiplayer_lobby(context) ==
                InitDetail::LobbyWaitResult::Reconfigure) {
                if (context.app_component() && context.app_component()->startScreenRequested)
                    throw Network::LobbyInterrupted("Return to start screen");
                continue;
            }
        }

        auto& player = context.session.player;
        auto& opponent = context.session.opponent;
        InitDetail::reset_player_board(player, context.config);
        seed_player(context, player, replayMode, multiplayer);
        InitDetail::initialize_player_field(context, player);
        player.presentation.margin=30;

        if (multiplayer) {
            InitDetail::exchange_multiplayer_player_setup(context, player, opponent);
            Engine::initialize_board(context, opponent, false);
            Engine::GarbageService::initialize(opponent, context.config.garbage.holePattern);
            if(Network::network_services(context).multiplayerRoom) Engine::initialize_multiplayer_scene(context);
        }

        // initialize_presentation prints the elapsed-time panel, so the old
        // round must be forgotten before it draws the countdown screen.
        context.session.gameStartTime.reset();
        if (!multiplayer && InitDetail::wait_for_game_terminal(context)==InitDetail::TerminalCheckResult::OpenSettings) {
            configure_after_terminal_check(context,nullptr);
            continue;
        }
        initialize_presentation(context, player, opponent, multiplayer);
        apply_initial_garbage(context, player);
        if(replayMode){
            Replay::restore_scheduled_playback_checkpoint(context);
        }
        return;
    }
}

} // namespace App
