#include "tetr/autoplay/strategy.h"
#include "tetr/app/application_state.h"
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/live_piece_controller.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/simulation_time.h"
#include "tetr/input/services.h"
#include "tetr/network/lobby_protocol.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/network/transport.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using Runtime::Duration;
using Runtime::TimePoint;
using Rules::PlayResult;

struct Fixture {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session, services};
    Replay::ReplaySessionState replay;
    Network::NetworkServices network;
    Input::Services input;
    Fixture(double pps = 2.0) {
        context.attach_replay(replay);
        context.attach_network(network);
        context.attach_input(input);
        context.config.simulationPps = pps;
        auto& p = session.player;
        p.gameplay.playfield.width = 10;
        p.gameplay.playfield.visibleHeight = 20;
        p.gameplay.playfield.nextCount = 5;
        p.pieces.allowedPieces = Piece::types_for_rotation_system(context.config.rotationSystem);
        Engine::initialize_board(context, p);
        Engine::GarbageService::initialize(p, context.config.garbage.holePattern);
        Engine::initialize_board_timers(context, p);
    }
    void advance() {
        assert(Engine::advance_piece_time(context, session.player) == PlayResult::ContinueFrame);
    }
};

void clocks_and_configuration() {
    UserConfig::Settings user;
    assert(Runtime::normalize_user_config(user, Runtime::GameMode::Zen).simulationPps == 0.0);
    const auto* setting = UserConfig::find_setting(UserConfig::Section::Game, "Simulation PPS");
    assert(setting && setting->id == UserConfig::SettingId::SimulationPps);
    assert(UserConfig::assign_setting_text(user, *setting, "2.5"));
    assert(user.turnPieces == 0.0);
    assert(UserConfig::assign_setting_text(user, UserConfig::SettingId::TurnPieces, "0.000001"));
    auto config = Runtime::normalize_user_config(user, Runtime::GameMode::QuickPlay);
    assert(config.simulationPps == 2.5);
    assert(config.turnPieces == 0.000001);
    const auto wire = Replay::encode_config_prefix(config) + '}';
    Replay::ConfigPrefixResult decoded;
    Runtime::RuntimeConfig copy;
    assert(Replay::decode_config_prefix(wire, copy, decoded));
    assert(copy.simulationPps == 2.5);
    assert(copy.turnPieces == 0.000001);
    assert(Replay::decode_config_prefix("{{Model,0}}", copy, decoded));
    assert(copy.simulationPps == 0.0);
    assert(copy.turnPieces == 0.0);
    assert(Replay::decode_config_prefix("{{SimulationPPS,2.5}}", copy, decoded));
    assert(copy.turnPieces == 1.0); // Old recorder omitted its default quota.
    assert(Replay::decode_config_prefix("{{SimulationPPS,2.5}{TurnPieces,0}}", copy, decoded));
    assert(copy.turnPieces == 0.0);
    user.simulationPps = std::numeric_limits<double>::quiet_NaN();
    assert(Runtime::normalize_user_config(user, Runtime::GameMode::Zen).simulationPps == 0.0);

    Fixture f;
    const auto realStart = f.context.real_now();
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    assert(f.context.now() == TimePoint{});
    assert(f.context.real_now() > realStart);
    f.advance();
    assert(f.context.now().seconds() == 0.5);
    f.advance();
    assert(f.context.now().seconds() == 1.0);
    Engine::initialize_board_timers(f.context, f.session.player);
    assert(f.context.now() == TimePoint{});
    f.context.config.simulationPps = 0;
    const auto before = f.context.now();
    f.advance();
    assert(f.session.completedSimulationPieces == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(f.context.now() > before);
    // Recorded simulation timestamps must still play on the replay wall clock.
    f.context.config.simulationPps = 2.0;
    f.context.config.mode.replayMode = true;
    assert(!f.context.uses_piece_time());
}

void timer_catchup_and_buffer_age() {
    Fixture f(0.4); // 2.5 simulated seconds per piece.
    auto& p = f.session.player;
    f.context.config.garbage.mode = Runtime::GarbageMode::CheeseTimer;
    f.context.config.garbage.generationInterval = Duration::from_seconds(0.5);
    f.advance();
    assert(f.context.now().seconds() == 2.5);
    assert(p.gameplay.playfield.garbageHeight == 5);
    assert(p.timing.timedGarbage.seconds() == 2.5);
    f.advance();
    assert(p.gameplay.playfield.garbageHeight == 10);
    assert(p.timing.timedGarbage.seconds() == 5.0);
    f.context.config.garbage.generationInterval = {};
    f.advance();
    assert(p.gameplay.playfield.garbageHeight == 11);

    Fixture b;
    auto& bp = b.session.player;
    b.context.config.garbage.mode = Runtime::GarbageMode::Backfire;
    b.context.config.garbage.incomingDelay = Duration::from_seconds(0.75);
    Engine::GarbageService::add_buffer(b.context, bp, bp.gameplay.playfield.field, 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Engine::GarbageService::update_buffer(b.context, bp);
    assert(bp.presentation.garbageBufferDisplay[1] == 1);
    b.advance();
    assert(bp.presentation.garbageBufferDisplay[1] == 2);
    b.advance();
    assert(bp.presentation.garbageBufferDisplay[1] == 3);
    Engine::GarbageService::offset_buffer(b.context, bp, bp.gameplay.playfield.field, 0, 0);
    assert(bp.garbage.buffer.empty());
    assert(bp.gameplay.playfield.garbageHeight == 3);

    Fixture quick(0.4);
    quick.context.config.garbage.mode = Runtime::GarbageMode::QuickPlay;
    quick.session.quickPlay.speed = 10;
    quick.advance();
    assert(quick.session.quickPlay.altitude > 0.0);
    assert(quick.session.player.timing.qpGravity.seconds() == 2.0);
    assert(Runtime::current_gravity_interval(quick.context).interval() < Duration::from_seconds(0.6));

    Fixture survival(0.1);
    survival.context.config.garbage.mode = Runtime::GarbageMode::Survival;
    survival.context.config.garbage.survivalApm = 1800;
    survival.services.random.seed(1234);
    survival.advance();
    assert(!survival.session.player.garbage.buffer.empty());
    assert(survival.session.player.garbage.buffer.front().arrival_time().seconds() < 10.0);
}

void complete_piece_and_animation() {
    Fixture f;
    auto& player = f.session.player;
    auto& field = player.gameplay.playfield.field;
    auto piece = Piece::get_definition(Piece::Type::from_index(0));
    auto movement = Engine::make_spawn_movement_state(10);
    while (piece.can_place(movement.pose.translated(1, 0), field)) ++movement.pose.position.row;
    // Fill the bottom row around the falling piece to guarantee a line clear.
    for (int c = 0; c < 10; ++c) {
        bool covered = false;
        for (const auto& cell : piece.cells(movement.pose.rotation))
            if (movement.pose.position.row + cell[0] == 19 &&
                movement.pose.position.col + cell[1] == c) covered = true;
        if (!covered) field.set_cell(19, c, Runtime::BoardCell::piece(1));
    }
    assert(piece.can_place(movement.pose, field));
    f.context.config.field.lineClearDelay = Duration::from_seconds(0.08);
    bool sawAnimation = false;
    const auto started = std::chrono::steady_clock::now();
    const auto result = Engine::finalize_locked_piece(f.context, field, piece, player, movement, 0, [&] {
        sawAnimation = true;
        assert(f.context.now().seconds() == 0.0);
        assert(f.session.completedSimulationPieces == 0);
        return Engine::run_frame(f.context, field, player);
    });
    assert(result == PlayResult::Locked);
    assert(sawAnimation);
    assert(std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(70));
    assert(player.gameplay.scoring.placedBlocks == 1);
    assert(player.gameplay.scoring.totalLines >= 1);
    assert(f.session.completedSimulationPieces == 1);
    assert(f.context.now().seconds() == 0.5);
    auto operations = Replay::decode_operation_stream(f.replay.data, 0);
    bool foundEnd = false;
    while (!operations.empty()) {
        const auto operation = operations.front();
        operations.pop();
        if (operation.kind == Replay::OperationKind::EndClearLine)
            assert(operation.timestamp.seconds() == 0.0);
        if (operation.kind == Replay::OperationKind::EndPlace) {
            foundEnd = true;
            assert(operation.timestamp.seconds() == 0.5);
        }
    }
    assert(foundEnd);
}

void human_gravity_and_lock_use_wall_time() {
    Fixture f;
    f.context.config.handling.gravityInterval =
        Runtime::GravityInterval::periodic(Duration::from_seconds(0.001));
    f.context.config.handling.epld = Duration::from_seconds(0.01);
    auto& p = f.session.player;
    auto& field = p.gameplay.playfield.field;
    auto piece = Piece::get_definition(p.pieces.bag.front().type);
    auto movement = Engine::make_spawn_movement_state(10);
    Engine::MovementInputState input;
    Engine::AutoplayCarryState carry;
    const auto start = std::chrono::steady_clock::now();
    const auto signal = Engine::run_live_piece_movement(f.context, field, piece, p,
        movement, input, carry, false,
        [&](Piece::Pose pose) { return piece.can_place(pose, field); },
        [&](Piece::Pose& pose, Runtime::Rotation target, Piece::KickIndex& kick) {
            return piece.try_rotate(pose, target, field, kick);
        }, [&] { return Engine::run_frame(f.context, field, p); });
    assert(signal == PlayResult::ContinueFrame);
    assert(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(20));
    assert(!piece.can_place(movement.pose.translated(1, 0), field));
    assert(f.context.now().seconds() == 0.0);
    assert(f.session.completedSimulationPieces == 0);
}

void autoplay_uses_wall_time() {
    struct Strategy : Autoplay::Strategy {
        int calls = 0;
        bool initialize(Runtime::GameContext&) override { return true; }
        Piece::Type choose_position(Runtime::GameContext&, Runtime::PlayerSessionState&,
            Runtime::Playfield field, const std::vector<Piece::Type>& next,
            const std::vector<Piece::Type>&, Piece::Pose start, Autoplay::HoldInput,
            Piece::Pose& answer, Runtime::RotationRecord& rotation) override {
            ++calls;
            auto piece = Piece::get_definition(next.front());
            answer = start;
            while (piece.can_place(answer.translated(1, 0), field)) ++answer.position.row;
            rotation = Runtime::RotationRecord::none();
            return next.front();
        }
        std::string movement_script(Runtime::GameContext&, Runtime::Playfield,
            Piece::Definition, Piece::Pose, Piece::Pose, Runtime::RotationRecord) override {
            return "H";
        }
    } strategy;
    Fixture f;
    f.context.attach_autoplay(strategy);
    auto& p = f.session.player;
    auto& field = p.gameplay.playfield.field;
    auto piece = Piece::get_definition(p.pieces.bag.front().type);
    auto movement = Engine::make_spawn_movement_state(10);
    Engine::AutoplayCarryState carry;
    auto step = [&] {
        return Engine::run_autoplay_step(f.context, field, piece, p, movement, carry,
            false, 10.0, true, Runtime::FoolMode::Disabled, Runtime::GravityInterval::disabled(),
            [&](Piece::Pose pose) { return piece.can_place(pose, field); },
            [&](Piece::Pose& pose, Runtime::Rotation target, Piece::KickIndex& kick) {
                return piece.try_rotate(pose, target, field, kick);
            }, [] { return PlayResult::ContinueFrame; });
    };
    // Planning starts immediately, but locking is paced by wall time even
    // while the piece-based simulation clock remains frozen.
    const auto started=f.context.real_now();
    assert(step().hardDrop && strategy.calls==1);
    assert((f.context.real_now()-started).seconds()>=.095);
    assert(f.context.now().seconds()==0.0);
    movement=Engine::make_spawn_movement_state(10);
    assert(step().hardDrop && strategy.calls==2);
    assert((f.context.real_now()-started).seconds()>=.195);
    assert(f.context.now().seconds()==0.0);
}

void turn_protocol_and_mixed_clocks() {
    Network::LobbyPlayerSetup setup;
    setup.width = 10;
    setup.visibleHeight = 20;
    setup.simulationPps = 2.5;
    setup.turnPieces = 0.000001;
    setup.playerIndex = 1;
    const auto wire = Network::encode_lobby_player_setup(setup);
    const auto decoded = Network::decode_lobby_player_setup(wire);
    assert(decoded && decoded->simulationPps == 2.5 && decoded->playerIndex == 1);
    assert(decoded->turnPieces == 0.000001);
    const auto legacy = Network::decode_lobby_player_setup("20;10;0;0;;0;0;0;piece-time-v1;2.5;0;");
    assert(legacy && legacy->turnPieces == 2.5);
    for (const auto quota : {"nan", "inf", "-1", "1e-99", "1000001"})
        assert(!Network::decode_lobby_player_setup(
            std::string("20;10;0;0;;0;0;0;piece-time-v2;2;0;") + quota + ";"));
    assert(!Network::decode_lobby_player_setup("20;10;0;0;;0;0;0;piece-time-v1;nan;0;"));
    assert(!Network::decode_lobby_player_setup("20;10;0;0;;0;0;0;piece-time-v1;2;2;"));
    const auto packet = Network::decode_frame_packet(Network::encode_piece_turn(42, 3), 10, 20);
    assert(packet && packet->type == Network::FramePacketType::PieceTurn);
    assert(packet->turnEpoch == 42 && packet->completedTurns == 3);
    assert(!Network::decode_frame_packet("!piece-turn;42;-1", 10, 20));

    Fixture a, b;
    a.context.config.turnPieces = b.context.config.turnPieces = 2.0;
    Network::connect_local_duplex(a.network, b.network);
    Network::configure_piece_turns(a.context, 2.0, 0, 42);
    Network::configure_piece_turns(b.context, 2.0, 1, 42);
    assert(a.network.pieceTurns.can_play());
    assert(!b.network.pieceTurns.can_play());
    Network::send(a.network, Network::encode_piece_turn(41, 100)); // previous round
    Network::send(a.network, Network::encode_piece_turn(42, 1));
    Network::send(a.network, Network::encode_piece_turn(42, 0)); // late duplicate
    assert(!Network::try_receive_frame(b.context, 10, 20));
    assert(b.network.pieceTurns.peerCompleted == 1);
    assert(b.network.pieceTurns.can_play());
    Network::configure_piece_turns(a.context, 0.0, 0, 43);
    b.context.config.simulationPps = 0.0;
    Network::configure_piece_turns(b.context, 2.0, 1, 43);
    assert(!a.network.pieceTurns.enabled && b.network.pieceTurns.enabled);
    b.context.config.turnPieces = 0.0;
    Network::configure_piece_turns(b.context, 2.0, 1, 43);
    assert(!b.network.pieceTurns.enabled);
    setup.simulationPps = 0.0;
    const auto wallSetup = Network::decode_lobby_player_setup(Network::encode_lobby_player_setup(setup));
    assert(wallSetup && wallSetup->turnPieces == setup.turnPieces);
    assert(wallSetup->simulationPps == 0.0);
    assert(a.context.uses_piece_time() && !b.context.uses_piece_time());
    a.context.config.garbage.mode = Runtime::GarbageMode::Multiplayer;
    a.context.config.mode.mode = Runtime::GameMode::LocalVersus;
    Network::send_round_signal(b.context, Network::OutgoingRoundSignal::LeaveRound);
    assert(Engine::advance_piece_time(a.context, a.session.player) == PlayResult::OpponentLeft);
    assert(a.context.now().seconds() == 0.5); // Never leave a completed tick half applied.
    assert(a.session.player.timing.frameUpdate.seconds() == 0.5);
}

void actual_piece_loop_alternation(double firstPps, double secondPps,
                                   double firstQuota, double secondQuota,
                                   int firstCount, const std::vector<int>& expected) {
    Fixture a(firstPps), b(secondPps);
    std::recursive_mutex outputMutex;
    a.context.attach_output_mutex(outputMutex);
    b.context.attach_output_mutex(outputMutex);
    Network::connect_local_duplex(a.network, b.network);
    a.context.config.turnPieces = firstQuota;
    b.context.config.turnPieces = secondQuota;
    for (auto* f : {&a, &b}) {
        f->context.config.garbage.mode = Runtime::GarbageMode::Multiplayer;
        f->context.config.mode.mode = Runtime::GameMode::LocalVersus;
    }
    Network::configure_piece_turns(a.context, secondQuota, 0, 99);
    Network::configure_piece_turns(b.context, firstQuota, 1, 99);
    std::mutex auditMutex;
    std::vector<int> audit;
    Engine::GameLoopExit outcomes[2]{};
    auto run = [&](Fixture& f, int side) {
        bool held = false;
        outcomes[side] = Engine::run_piece_loop(f.context, f.session.player,
            [&](Runtime::Playfield&, Piece::Definition, Runtime::PlayerSessionState& p, bool hold) {
                if (side == 0 && !held) {
                    held = true;
                    assert(!hold && f.session.completedSimulationPieces == 0);
                    return PlayResult::Hold;
                }
                {
                    std::lock_guard<std::mutex> lock(auditMutex);
                    audit.push_back(side + 1);
                }
                ++p.gameplay.scoring.placedBlocks;
                f.advance();
                if (side == 0 && p.gameplay.scoring.placedBlocks == firstCount) {
                    Network::send_round_signal(f.context, Network::OutgoingRoundSignal::LocalLoss);
                    return PlayResult::Dead;
                }
                return PlayResult::Locked;
            });
    };
    std::thread second([&] { run(b, 1); }); // Start waiting side first.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    {
        std::lock_guard<std::mutex> lock(auditMutex);
        assert(audit.empty()); // No spawn or movement before Player 1's window.
    }
    std::thread first([&] { run(a, 0); });
    first.join();
    second.join();
    assert(outcomes[0] == Engine::GameLoopExit::Dead);
    assert(outcomes[1] == Engine::GameLoopExit::Won);
    assert(audit == expected);
    assert(a.session.completedSimulationPieces == (firstPps > 0 ? static_cast<unsigned>(firstCount) : 0));
}

void real_clock_pauses_while_waiting() {
    Fixture a(0), b(0);
    Network::connect_local_duplex(a.network,b.network);
    a.context.config.turnPieces=1;
    a.context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    a.context.config.mode.mode=Runtime::GameMode::LocalVersus;
    Network::configure_piece_turns(a.context,1,0,123);
    a.session.player.gameplay.scoring.placedBlocks=1;
    const auto gameBefore=a.context.now();
    const auto wallBefore=a.context.real_now();
    std::thread peer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        Network::send(b.network,Network::encode_piece_turn(123,1));
    });
    assert(Engine::wait_for_piece_turn(a.context,a.session.player)==PlayResult::ContinueFrame);
    peer.join();
    assert((a.context.real_now()-wallBefore).seconds()>=0.1);
    assert((a.context.now()-gameBefore).seconds()<0.05);
    assert(a.session.turnWaitDuration.seconds()>=0.1);
    assert(!a.session.turnWaitStarted);
    assert(a.replay.data.find("pause_game_time")!=std::string::npos);
    assert(a.replay.data.find("resume_game_time")!=std::string::npos);
    const auto resumed=a.context.now();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert((a.context.now()-resumed).seconds()>=0.015);
    Engine::initialize_board_timers(a.context,a.session.player);
    assert(a.session.turnWaitDuration==Duration{});
}

void quota_boundaries() {
    Network::PieceTurnState turns;
    turns.piecesPerTurn = 0.1;
    turns.complete_pieces(1);
    assert(turns.completed == 10);
    turns.complete_pieces(1000000);
    assert(turns.completed == 10000000);
    turns.piecesPerTurn = 0.000001;
    turns.complete_pieces(1);
    assert(turns.completed == 1000000);
    turns.piecesPerTurn = 1e-9;
    turns.complete_pieces(1);
    assert(turns.completed == 1000000000);
    turns.piecesPerTurn = 2.5;
    turns.complete_pieces(2);
    assert(turns.completed == 0);
    turns.complete_pieces(3);
    assert(turns.completed == 1);
    turns.complete_pieces(5);
    assert(turns.completed == 2);
}

void subject_only_piece_limit() {
    const auto set_variable = [](const char* name, const char* value) {
#ifdef _WIN32
        _putenv_s(name, value ? value : "");
#else
        if (value) setenv(name, value, 1);
        else unsetenv(name);
#endif
    };
    const char* pieceName = "TETR_TEST_MAX_PIECES";
    const char* sideName = "TETR_TEST_MAX_PIECES_FIRST_ONLY";
    const std::optional<std::string> oldPieces = std::getenv(pieceName)
        ? std::optional<std::string>(std::getenv(pieceName)) : std::nullopt;
    const std::optional<std::string> oldSide = std::getenv(sideName)
        ? std::optional<std::string>(std::getenv(sideName)) : std::nullopt;
    set_variable(pieceName, "2");
    for (bool firstOnly : {false, true}) {
        set_variable(sideName, firstOnly ? "1" : nullptr);
        Fixture first, second;
        App::ApplicationState app;
        app.primaryContext = &first.context;
        app.localPlayerTwoContext = &second.context;
        for (auto* fixture : {&first, &second}) {
            fixture->context.attach_app(app);
            const auto outcome = Engine::run_piece_loop(fixture->context, fixture->session.player,
                [](Runtime::Playfield&, Piece::Definition, Runtime::PlayerSessionState& player, bool) {
                    return ++player.gameplay.scoring.placedBlocks == 3
                        ? PlayResult::Dead : PlayResult::Locked;
                });
            const bool uncapped = firstOnly && fixture == &second;
            assert(outcome == (uncapped ? Engine::GameLoopExit::Dead : Engine::GameLoopExit::GoalReached));
            assert(fixture->session.player.gameplay.scoring.placedBlocks == (uncapped ? 3 : 2));
        }
    }
    set_variable(pieceName, oldPieces ? oldPieces->c_str() : nullptr);
    set_variable(sideName, oldSide ? oldSide->c_str() : nullptr);
}
} // namespace

int main() {
    Render::begin_output_suppression();
    clocks_and_configuration();
    timer_catchup_and_buffer_age();
    complete_piece_and_animation();
    human_gravity_and_lock_use_wall_time();
    autoplay_uses_wall_time();
    turn_protocol_and_mixed_clocks();
    quota_boundaries();
    subject_only_piece_limit();
    real_clock_pauses_while_waiting();
    actual_piece_loop_alternation(2.5, 1.5, 2.5, 1.5, 6, {1,1,1,2,2,1,1,2,1});
    actual_piece_loop_alternation(0.5, 1.0, 0.5, 1.0, 2, {1,2,2,1});
    actual_piece_loop_alternation(2.5, 1.5, 1.0, 1.0, 3, {1,2,1,2,1});
    actual_piece_loop_alternation(2.5, 1.5, 0.000001, 0.000001, 3, {1,2,1,2,1});
    actual_piece_loop_alternation(2.5, 1.5, 0.000001, 0.000002, 3, {1,2,2,1,2,2,1});
    actual_piece_loop_alternation(2.5, 1.5, 1e-9, 1e-9, 3, {1,2,1,2,1});
    actual_piece_loop_alternation(0.0, 0.0, 1.0, 1.0, 3, {1,2,1,2,1});
    actual_piece_loop_alternation(0.0, 1.5, 2.5, 1.5, 6, {1,1,1,2,2,1,1,2,1});
    std::cerr << "simulation_time_test: OK\n";
}
