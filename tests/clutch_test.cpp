#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/render/board_renderer.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {

struct Capture {
    std::ostringstream stream;
    std::streambuf* original = std::cout.rdbuf(stream.rdbuf());
    ~Capture() { std::cout.rdbuf(original); }
};

struct Fixture {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session, services};

    Fixture() {
        context.config.field.width = 10;
        context.config.field.height = 20;
        context.config.field.nextCount = 0;
        auto& player = session.player;
        player.presentation.margin = 30;
        player.gameplay.playfield.width = 10;
        player.gameplay.playfield.visibleHeight = 20;
        player.gameplay.playfield.nextCount = 0;
        player.gameplay.playfield.field.initialize(10, 20);
    }
};

void block_spawn_cell(
    Runtime::Playfield& field,
    const Piece::Definition& piece,
    const Engine::MovementState& state) {
    const auto offset = piece.cells(state.pose.rotation).front();
    field.set_cell(
        state.pose.position.row + offset[0],
        state.pose.position.col + offset[1],
        Runtime::BoardCell::piece(1));
}

void test_requires_previous_line_clear() {
    Fixture fixture;
    auto& player = fixture.session.player;
    auto& field = player.gameplay.playfield.field;
    Piece::Definition piece = Piece::get_definition(Piece::Type::from_index(0));
    Engine::MovementState state = Engine::make_spawn_movement_state(field.width());
    block_spawn_cell(field, piece, state);

    Capture output;
    assert(Engine::handle_spawn_collision(
        fixture.context, field, piece, player, state));
    assert(state.pose.position.row == -2);
    assert(!player.clutch.currentTurnLifted);
    assert(player.clutch.consecutive == 0);
}

void test_lifts_in_every_game_mode_and_after_hold() {
    constexpr std::array<Runtime::GameMode, 7> modes = {
        Runtime::GameMode::Zen,
        Runtime::GameMode::Racing,
        Runtime::GameMode::Blitz,
        Runtime::GameMode::QuickPlay,
        Runtime::GameMode::Multiplayer,
        Runtime::GameMode::Replay,
        Runtime::GameMode::LocalVersus};

    Capture output;
    for (Runtime::GameMode mode : modes) {
        Fixture fixture;
        fixture.context.config.mode.mode = mode;
        fixture.context.config.mode.replayMode = mode == Runtime::GameMode::Replay;
        auto& player = fixture.session.player;
        auto& field = player.gameplay.playfield.field;
        Piece::Definition piece = Piece::get_definition(Piece::Type::from_index(0));
        Engine::MovementState state = Engine::make_spawn_movement_state(field.width());
        block_spawn_cell(field, piece, state);
        player.clutch.previousPieceCleared = true;

        assert(!Engine::handle_spawn_collision(
            fixture.context, field, piece, player, state));
        assert(state.pose.position.row == -3);
        assert(piece.can_place(state.pose, field));
        assert(player.clutch.currentTurnLifted);
        assert(player.clutch.consecutive == 1);
    }

    // A normally spawned next piece does not consume the eligibility.  If it
    // is held, the replacement piece can still receive the Clutch lift.
    Fixture fixture;
    auto& player = fixture.session.player;
    auto& field = player.gameplay.playfield.field;
    Piece::Definition piece = Piece::get_definition(Piece::Type::from_index(0));
    player.clutch.previousPieceCleared = true;
    Engine::MovementState beforeHold = Engine::make_spawn_movement_state(field.width());
    assert(!Engine::handle_spawn_collision(
        fixture.context, field, piece, player, beforeHold));
    assert(!player.clutch.currentTurnLifted);

    Engine::MovementState afterHold = Engine::make_spawn_movement_state(field.width());
    block_spawn_cell(field, piece, afterHold);
    assert(!Engine::handle_spawn_collision(
        fixture.context, field, piece, player, afterHold));
    assert(afterHold.pose.position.row == -3);
    assert(player.clutch.currentTurnLifted);

    // If the first piece itself Clutches and is then held, a blocked Hold
    // replacement is another consecutive trigger (DOUBLE CLUTCH).
    Fixture doubleFixture;
    auto& doublePlayer = doubleFixture.session.player;
    auto& doubleField = doublePlayer.gameplay.playfield.field;
    doublePlayer.clutch.previousPieceCleared = true;
    Engine::MovementState clutchedBeforeHold =
        Engine::make_spawn_movement_state(doubleField.width());
    block_spawn_cell(doubleField, piece, clutchedBeforeHold);
    assert(!Engine::handle_spawn_collision(
        doubleFixture.context,
        doubleField,
        piece,
        doublePlayer,
        clutchedBeforeHold));
    assert(doublePlayer.clutch.consecutive == 1);
    Engine::MovementState clutchedAfterHold =
        Engine::make_spawn_movement_state(doubleField.width());
    assert(!Engine::handle_spawn_collision(
        doubleFixture.context,
        doubleField,
        piece,
        doublePlayer,
        clutchedAfterHold));
    assert(clutchedAfterHold.pose.position.row == -3);
    assert(doublePlayer.clutch.consecutive == 2);
}

void test_consecutive_state_and_height_limit() {
    Capture output;
    Fixture fixture;
    auto& player = fixture.session.player;
    auto& field = player.gameplay.playfield.field;
    Piece::Definition piece = Piece::get_definition(Piece::Type::from_index(0));

    Engine::record_locked_piece_for_clutch(player, 1);
    assert(player.clutch.previousPieceCleared);
    assert(player.clutch.consecutive == 0);

    Engine::MovementState first = Engine::make_spawn_movement_state(field.width());
    block_spawn_cell(field, piece, first);
    assert(!Engine::handle_spawn_collision(
        fixture.context, field, piece, player, first));
    assert(player.clutch.consecutive == 1);
    Engine::record_locked_piece_for_clutch(player, 1);
    assert(player.clutch.previousPieceCleared);
    assert(!player.clutch.currentTurnLifted);
    assert(player.clutch.consecutive == 1);

    field.clear();
    Engine::MovementState second = Engine::make_spawn_movement_state(field.width());
    block_spawn_cell(field, piece, second);
    assert(!Engine::handle_spawn_collision(
        fixture.context, field, piece, player, second));
    assert(player.clutch.consecutive == 2);
    Engine::record_locked_piece_for_clutch(player, 0);
    assert(!player.clutch.previousPieceCleared);
    assert(!player.clutch.currentTurnLifted);
    assert(player.clutch.consecutive == 0);

    field.clear();
    player.clutch.previousPieceCleared = true;
    Engine::MovementState blocked = Engine::make_spawn_movement_state(field.width());
    const auto offset = piece.cells(blocked.pose.rotation).front();
    for (int row = blocked.pose.position.row;
         row >= -Runtime::kHiddenRowCapacity;
         --row) {
        field.set_cell(
            row + offset[0],
            blocked.pose.position.col + offset[1],
            Runtime::BoardCell::piece(2));
    }
    assert(Engine::handle_spawn_collision(
        fixture.context, field, piece, player, blocked));
}

void test_popup_and_local_result_banner() {
    Fixture fixture;
    auto& player = fixture.session.player;
    {
        Capture output;
        Render::print_clutch(fixture.context, player, 10);
        Render::print_clutch(fixture.context, player, 11);
        assert(output.stream.str().find("DECA CLUTCH") != std::string::npos);
        assert(output.stream.str().find("11x CLUTCH") != std::string::npos);
    }

    fixture.context.config.mode.mode = Runtime::GameMode::LocalVersus;
    // Won/OpponentLeft historically rendered their banner independent of the
    // garbage transport, so this test needs no live network component.
    fixture.context.config.garbage.mode = Runtime::GarbageMode::None;
    player.pieces.bag.push_back({Piece::Type::from_index(0), false});
    Capture output;
    const Engine::GameLoopExit result = Engine::run_piece_loop(
        fixture.context,
        player,
        [](Runtime::Playfield&,
           Piece::Definition,
           Runtime::PlayerSessionState&,
           bool) { return Rules::PlayResult::Won; });
    assert(result == Engine::GameLoopExit::Won);
    assert(output.stream.str().find("You won this game!") == std::string::npos);
}

} // namespace

int main() {
#ifdef __linux__
    setenv("TERM", "xterm", 1);
#endif
    test_requires_previous_line_clear();
    test_lifts_in_every_game_mode_and_after_hold();
    test_consecutive_state_and_height_limit();
    test_popup_and_local_result_banner();
    std::cerr << "clutch_test: OK\n";
}
