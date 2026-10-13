#include "tetr/app/application_state.h"
#include "tetr/app/init.h"
#include "tetr/app/settings_menu.h"
#include "tetr/autoplay/strategy.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/input/services.h"
#include "tetr/network/frame_packet.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/local_versus_codec.h"
#include "tetr/replay/playback_checkpoint.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/runtime/game_context.h"
#include "tetr/render/board_renderer.h"
#include "tetr/config/user_config.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

std::size_t count_occurrences(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    for (std::size_t position = 0;
         (position = text.find(needle, position)) != std::string::npos;
         position += needle.size()) {
        ++count;
    }
    return count;
}

struct FakeStrategy final : Autoplay::Strategy {
    int initializeCalls = 0;
    Runtime::GameContext* initializedContext = nullptr;

    bool initialize(Runtime::GameContext& context) override {
        ++initializeCalls;
        initializedContext = &context;
        return true;
    }

    Piece::Type choose_position(
        Runtime::GameContext&,
        Runtime::PlayerSessionState&,
        Runtime::Playfield,
        const std::vector<Piece::Type>& next,
        const std::vector<Piece::Type>&,
        Piece::Pose,
        Autoplay::HoldInput,
        Piece::Pose&,
        Runtime::RotationRecord&) override {
        return next.empty() ? Piece::Type::none() : next.front();
    }

    std::string movement_script(
        Runtime::GameContext&,
        Runtime::Playfield,
        Piece::Definition,
        Piece::Pose,
        Piece::Pose,
        Runtime::RotationRecord) override {
        return "H";
    }
};

struct LocalPlayerFixture {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session, services};
    Network::NetworkServices network;
    Replay::ReplaySessionState replay;
    Input::Services input;
    UserConfig::Settings settings;
    FakeStrategy autoplay;

    LocalPlayerFixture() {
        context.attach_network(network);
        context.attach_replay(replay);
        context.attach_input(input);
        context.attach_user_settings(settings);
        context.attach_autoplay(autoplay);
    }
};

} // namespace

int main() {
#ifdef __linux__
    setenv("TERM", "xterm", 1);
#endif

    LocalPlayerFixture first;
    LocalPlayerFixture second;
    App::ApplicationState app;
    first.context.attach_app(app);
    second.context.attach_app(app);
    first.context.attach_output_mutex(app.terminalOutputMutex);
    second.context.attach_output_mutex(app.terminalOutputMutex);
    app.primaryContext = &first.context;
    app.localPlayerTwoContext = &second.context;

    Input::share_key_state(second.input, first.input);
    assert(Input::shares_key_state_with(second.input, first.input));

    // Entering Local Versus starts both players from the settings currently
    // being edited, including controls, without merging their save paths.
    first.settings.gameMode = 6;
    first.settings.fieldWidth = 17;
    first.settings.autoplayPps = 4.25;
    first.settings.keyCodes = UserConfig::default_key_codes();
    first.settings.keybindingPath = "player_one_keys.txt";
    second.settings.fieldWidth = 8;
    second.settings.autoplayPps = 99.0;
    second.settings.keyCodes = UserConfig::default_player_two_key_codes();
    second.settings.keybindingPath = "player_two_keys.txt";
    App::Settings::initialize_local_versus_player_settings(first.context);
    assert(second.settings.gameMode == 6);
    assert(second.settings.fieldWidth == 17);
    assert(second.settings.autoplayPps == 4.25);
    assert(second.settings.keyCodes == first.settings.keyCodes);
    assert(first.settings.keybindingPath == "player_one_keys.txt");
    assert(second.settings.keybindingPath == "player_two_keys.txt");
    assert(second.input.bindings.entries() == first.input.bindings.entries());

    // Leaving Local Versus from Player 2 promotes the settings currently on
    // screen to the normal single-player context and keeps the main save path.
    second.settings.gameMode = 1;
    second.settings.fieldWidth = 13;
    second.settings.autoplayPps = 7.5;
    second.settings.keyCodes = UserConfig::default_player_two_key_codes();
    app.playerTwoSettingsMenu.selectedEntries["root"] = "GAME";
    app.playerTwoSettingsMenu.selectedEntries["root/GAME"] = "pieces";
    App::Settings::adopt_single_player_settings(second.context);
    assert(first.settings.gameMode == 1);
    assert(first.settings.fieldWidth == 13);
    assert(first.settings.autoplayPps == 7.5);
    assert(first.settings.keyCodes == second.settings.keyCodes);
    assert(first.settings.keybindingPath == "player_two_keys.txt");
    assert(first.input.bindings.entries() == second.input.bindings.entries());
    assert(first.context.config.mode.mode == Runtime::GameMode::Racing);
    assert(app.playerTwoSettingsMenu.selectedEntries.at("root") == "GAME");
    assert(app.playerTwoSettingsMenu.selectedEntries.at("root/GAME") == "pieces");
    assert(app.savedPlayerOneSettings->fieldWidth == 17);
    first.settings.fieldWidth = 15;
    App::Settings::initialize_local_versus_player_settings(first.context);
    assert(first.settings.fieldWidth == 17);
    assert(second.settings.fieldWidth == 15);
    assert(first.settings.keybindingPath == "player_one_keys.txt");

    first.services.random.seed(12345);
    second.services.random.seed(67890);

    first.settings.gameMode = 6;
    first.settings.fieldWidth = 10;
    first.settings.fieldHeight = 20;
    first.settings.nextCount = 5;
    first.settings.holdEnabled = 1;
    first.settings.gravityInterval = 0.55;
    first.settings.das = 0.08;
    first.settings.autoplayPps = 2.5;
    first.settings.autoplayWidth = 40;
    first.settings.squareStyle = 0;
    first.settings.obscuredColors = 0;

    second.settings.gameMode = 6;
    second.settings.fieldWidth = 12;
    second.settings.fieldHeight = 24;
    second.settings.nextCount = 3;
    second.settings.holdEnabled = 0;
    second.settings.gravityInterval = 0.2;
    second.settings.das = 0.15;
    second.settings.autoplayPps = 8.0;
    second.settings.autoplayWidth = 125;
    second.settings.squareStyle = 1;
    second.settings.obscuredColors = 1;

    // A previous round must not leak its elapsed time onto either player's
    // board while the next round is still counting down.
    first.session.gameStartTime =
        first.context.now() - Runtime::Duration::from_seconds(42.0);
    second.session.gameStartTime =
        second.context.now() - Runtime::Duration::from_seconds(17.0);
    first.session.player.clutch.previousPieceCleared = true;
    first.session.player.clutch.currentTurnLifted = true;
    first.session.player.clutch.consecutive = 4;
    first.session.player.presentation.spike.attack = 12;
    first.session.player.presentation.spike.lastAttackAt =
        Runtime::TimePoint::from_seconds(10.0);
    second.session.player.clutch.previousPieceCleared = true;
    second.session.player.clutch.consecutive = 2;
    second.session.player.gameplay.scoring.totalOutgoingAttack = 12;
    second.session.player.gameplay.scoring.difficultClears = 3;
    second.session.player.presentation.spike.attack = 9;
    second.session.player.presentation.spike.lastAttackAt =
        Runtime::TimePoint::from_seconds(10.0);

    std::ostringstream rendered;
    std::streambuf* originalOutput = std::cout.rdbuf(rendered.rdbuf());
    App::initialize_game(first.context);
    std::cout.rdbuf(originalOutput);
    assert(rendered.str().find("HOLD") != std::string::npos);
    assert(rendered.str().find("NEXT") != std::string::npos);
    assert(count_occurrences(rendered.str(), "0.00 S") >= 2);
    assert(!first.session.gameStartTime);
    assert(!second.session.gameStartTime);
    assert(!first.session.player.clutch.previousPieceCleared);
    assert(!first.session.player.clutch.currentTurnLifted);
    assert(first.session.player.clutch.consecutive == 0);
    assert(!second.session.player.clutch.previousPieceCleared);
    assert(second.session.player.clutch.consecutive == 0);
    assert(second.session.player.gameplay.scoring.totalOutgoingAttack == 0);
    assert(second.session.player.gameplay.scoring.difficultClears == 0);
    assert(first.session.player.presentation.spike.attack == 0);
    assert(!first.session.player.presentation.spike.lastAttackAt);
    assert(second.session.player.presentation.spike.attack == 0);
    assert(!second.session.player.presentation.spike.lastAttackAt);

    assert(first.context.config.mode.mode == Runtime::GameMode::LocalVersus);
    assert(second.context.config.mode.mode == Runtime::GameMode::LocalVersus);
    assert(first.context.config.garbage.mode == Runtime::GarbageMode::Multiplayer);
    assert(second.context.config.garbage.mode == Runtime::GarbageMode::Multiplayer);

    assert(first.context.config.field.width == 10);
    assert(first.context.config.field.height == 20);
    assert(first.context.config.field.nextCount == 5);
    assert(first.context.config.field.holdEnabled);
    assert(first.context.config.handling.gravityInterval.interval().seconds() == 0.55);
    assert(first.context.config.handling.das.seconds() == 0.08);
    assert(first.context.config.autoplay.pps == 2.5);
    assert(first.context.config.autoplay.searchWidth == 40);
    assert(first.context.config.display.squareStyle == 0);
    assert(!first.context.config.display.obscuredColors);

    assert(second.context.config.field.width == 12);
    assert(second.context.config.field.height == 24);
    assert(second.context.config.field.nextCount == 3);
    assert(!second.context.config.field.holdEnabled);
    assert(second.context.config.handling.gravityInterval.interval().seconds() == 0.2);
    assert(second.context.config.handling.das.seconds() == 0.15);
    assert(second.context.config.autoplay.pps == 8.0);
    assert(second.context.config.autoplay.searchWidth == 125);
    assert(second.context.config.display.squareStyle == 0);
    assert(!second.context.config.display.obscuredColors);
    assert(second.settings.squareStyle == first.settings.squareStyle);
    assert(second.settings.obscuredColors == first.settings.obscuredColors);

    assert(first.session.player.gameplay.playfield.width == 10);
    assert(second.session.player.gameplay.playfield.width == 12);
    assert(first.session.player.gameplay.playfield.visibleHeight == 20);
    assert(second.session.player.gameplay.playfield.visibleHeight == 24);
    assert(first.session.player.presentation.margin == 30);
    assert(second.session.player.presentation.margin >
           first.session.player.presentation.margin + first.settings.fieldWidth * 2);

    // In multiplayer layouts the second player's 19-column SPIKE band starts
    // at margin-22.  It must begin strictly after the first player's NEXT
    // panel, including the panel's right border.
    const int firstNextRight = first.session.player.presentation.margin +
        first.session.player.gameplay.playfield.width * 2 +
        first.session.player.presentation.blockMaxWidth * 2 + 3;
    const int secondSpikeLeft =
        second.session.player.presentation.margin - 22;
    assert(secondSpikeLeft > firstNextRight);

    // Both sides use a shared match seed. With compatible piece sets this
    // gives both players the same fair bag without sharing mutable queue state.
    assert(Engine::current_piece(first.session.player) ==
           Engine::current_piece(second.session.player));
    assert(first.session.player.pieces.bag == second.session.player.pieces.bag);
    first.session.player.pieces.bag.pop_front();
    assert(first.session.player.pieces.bag != second.session.player.pieces.bag);

    assert(Network::uses_local_duplex(first.network));
    assert(Network::uses_local_duplex(second.network));
    assert(!first.network.initialized);
    assert(!second.network.initialized);

    // Local Versus already renders each real player through its own context.
    // Receiving an attack must not draw a duplicate SPIKE using the hidden
    // opponent mirror, whose default margin would produce a negative cursor
    // column and make the label jump around the terminal.
    Network::send_attack_frame(
        first.context,
        {Runtime::MultiplayerSequenceId::from_wire(1),
         Runtime::MultiplayerSequenceId::from_wire(0),
         8});
    second.session.player.timing.frameUpdate = second.context.now();
    std::ostringstream frameRendered;
    originalOutput = std::cout.rdbuf(frameRendered.rdbuf());
    const auto frameResult = Engine::run_frame(
        second.context,
        second.session.player.gameplay.playfield.field,
        second.session.player);
    std::cout.rdbuf(originalOutput);
    assert(frameResult == Rules::PlayResult::ContinueFrame);
    assert(second.session.opponent.presentation.spike.attack == 0);
    assert(!second.session.opponent.presentation.spike.lastAttackAt);
    assert(frameRendered.str().find("SPIKE") == std::string::npos);

    Network::send_attack_frame(
        first.context,
        {Runtime::MultiplayerSequenceId::from_wire(1),
         Runtime::MultiplayerSequenceId::from_wire(0),
         4});
    const auto packet = Network::try_receive_frame(
        second.context,
        second.context.config.field.width,
        second.context.config.field.height);
    assert(packet);
    assert(packet->type == Network::FramePacketType::Attack);
    assert(packet->attack == 4);

    // A B2B charge release is transmitted as three independently timed garbage
    // packets.  Ordinary attack from the same clear remains a separate packet.
    Runtime::Playfield releaseField;
    releaseField.initialize(10, 20);
    releaseField.clear();
    const int released = Engine::GarbageService::offset_buffer(
        first.context, first.session.player, releaseField, 14, 1, 10);
    assert(released == 14);
    const std::array<int, 4> expectedRelease{{4, 3, 3, 4}};
    for (int expected : expectedRelease) {
        const auto releasePacket = Network::try_receive_frame(
            second.context,
            second.context.config.field.width,
            second.context.config.field.height);
        assert(releasePacket);
        assert(releasePacket->type == Network::FramePacketType::Attack);
        assert(releasePacket->attack == expected);
    }
    assert(!Network::try_receive_frame(
        second.context,
        second.context.config.field.width,
        second.context.config.field.height));
    assert(first.session.player.multiplayer.pendingSends.size() == 4);

    // The composition also owns one Strategy per player. A second match can
    // therefore initialize two independently selected bots with independent
    // PPS/strength values instead of sharing a single strategy object.
    first.settings.autoplayMode = 1;
    second.settings.autoplayMode = 4;
#ifdef __linux__
    setenv("TETR_LOCAL_VERSUS_SEED", "424242", 1);
#endif
    std::ostringstream botRendered;
    originalOutput = std::cout.rdbuf(botRendered.rdbuf());
    App::initialize_game(first.context);
    std::cout.rdbuf(originalOutput);
#ifdef __linux__
    unsetenv("TETR_LOCAL_VERSUS_SEED");
#endif
    assert(first.autoplay.initializeCalls == 1);
    assert(second.autoplay.initializeCalls == 1);
    assert(first.autoplay.initializedContext == &first.context);
    assert(second.autoplay.initializedContext == &second.context);
    assert(first.context.config.autoplay.mode == Runtime::AutoplayMode::While1);
    assert(second.context.config.autoplay.mode == Runtime::AutoplayMode::Zzztoj);
    assert(first.context.config.autoplay.pps == 2.5);
    assert(second.context.config.autoplay.pps == 8.0);
    assert(first.context.config.autoplay.searchWidth == 40);
    assert(second.context.config.autoplay.searchWidth == 125);
#ifdef __linux__
    assert(first.replay.configPrefix.find("{garbage_seed,424242}") != std::string::npos);
    assert(second.replay.configPrefix.find("{garbage_seed,424242}") != std::string::npos);
    assert(first.replay.configPrefix.find("{block_seed," +
        std::to_string(424242u ^ Runtime::kMultiplayerBlockSeedSalt) + "}") !=
        std::string::npos);
#endif

    // A Local Versus replay contains both independently configured streams.
    // Selecting Replay loads and lays out both boards instead of falling back
    // to the historical single-board playback path.
    const std::string firstReplay = first.replay.configPrefix + "{end_all,4.000000}";
    const std::string secondReplay = second.replay.configPrefix + "{end_all,6.000000}";
    const std::string dualReplay = Replay::encode_local_versus_replay(
        firstReplay, secondReplay);
    const std::filesystem::path replayPath =
        std::filesystem::path("replay") / "tetr_local_versus_replay_test.txt";
    std::filesystem::create_directories(replayPath.parent_path());
    {
        std::ofstream output(replayPath, std::ios::trunc);
        output << dualReplay << Replay::replay_hash_suffix(dualReplay);
    }
    first.settings.gameMode = 5;
    first.settings.replayPath = replayPath.filename().string();
    first.settings.replayTimeMultiplier = 2.0;
    std::ostringstream replayRendered;
    originalOutput = std::cout.rdbuf(replayRendered.rdbuf());
    App::initialize_game(first.context);
    std::cout.rdbuf(originalOutput);

    assert(first.settings.replayPath == replayPath.generic_u8string());
    assert(first.replay.localVersusReplay);
    assert(second.replay.localVersusReplay);
    assert(first.context.config.mode.mode == Runtime::GameMode::LocalVersus);
    assert(second.context.config.mode.mode == Runtime::GameMode::LocalVersus);
    assert(first.context.config.mode.replayMode);
    assert(second.context.config.mode.replayMode);
    assert(first.context.config.field.width == 10);
    assert(second.context.config.field.width == 12);
    assert(first.session.player.gameplay.playfield.width == 10);
    assert(second.session.player.gameplay.playfield.width == 12);
    assert(second.session.player.presentation.margin >
           first.session.player.presentation.margin + first.context.config.field.width * 2);
    assert(!Network::uses_local_duplex(first.network));
    assert(!Network::uses_local_duplex(second.network));
    assert(first.replay.operations.size() == 1);
    assert(second.replay.operations.size() == 1);
    assert(first.replay.operations.front().kind == Replay::OperationKind::EndAll);
    assert(second.replay.operations.front().kind == Replay::OperationKind::EndAll);
    assert(first.replay.operations.front().timestamp.seconds() == 4.0);
    assert(second.replay.operations.front().timestamp.seconds() == 6.0);
    // The legacy config value remains readable, but playback now always
    // starts at 1x and speed changes are session-only controls.
    assert(first.replay.playback.speed == 1.0);
    assert(second.replay.playback.speed == 1.0);
    assert(first.replay.playback.duration.seconds() == 6.0);
    assert(second.replay.playback.duration.seconds() == 6.0);
    assert(first.replay.playback.progressVisible);
    assert(!second.replay.playback.progressVisible);
    assert(first.replay.playback.progressRow == second.replay.playback.progressRow);
    const auto replayFooter=[](Runtime::GameContext& context,const Runtime::PlayerSessionState& player) {
        return Render::board_vertical_extent(context,player,false).footer+
            (context.config.garbage.mode==Runtime::GarbageMode::QuickPlay ? 4 : 2);
    };
    assert(first.replay.playback.progressRow==
        std::max(replayFooter(first.context,first.session.player),
                 replayFooter(second.context,second.session.player)));
    assert(count_occurrences(replayRendered.str(), "HOLD") >= 1);
    assert(count_occurrences(replayRendered.str(), "NEXT") >= 2);

    Replay::start_playback(first.context,first.context.now());
    Replay::start_playback(second.context,second.context.now());
    Replay::capture_playback_checkpoint(first.context);
    Replay::capture_playback_checkpoint(second.context);
    assert(Replay::playback_checkpoint_count(first.context)==1);
    assert(Replay::playback_checkpoint_count(second.context)==1);

    // A seek request survives the game-loop unwind, reloads both streams,
    // and prepares both players at the shared target while preserving the
    // current pause/speed state.
    app.replayResume={true,5.0,2.0,true};
    std::ostringstream seekRendered;
    originalOutput=std::cout.rdbuf(seekRendered.rdbuf());
    App::initialize_game(first.context);
    std::cout.rdbuf(originalOutput);
    assert(!app.replayResume.pending);
    assert(first.replay.playback.resuming);
    assert(second.replay.playback.resuming);
    assert(first.replay.playback.elapsed.seconds()==5.0);
    assert(second.replay.playback.elapsed.seconds()==5.0);
    assert(first.replay.playback.speed==2.0);
    assert(second.replay.playback.speed==2.0);
    assert(first.replay.playback.paused);
    assert(second.replay.playback.paused);
    assert(first.replay.playback.duration.seconds()==6.0);
    assert(Replay::playback_checkpoint_count(first.context)==1);
    assert(Replay::playback_checkpoint_count(second.context)==1);
    std::filesystem::remove(replayPath);

    std::cerr << "local_versus_init_test: OK\n";
}
