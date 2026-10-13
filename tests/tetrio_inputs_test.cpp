#include "tetr/replay/tetrio_inputs.h"
#include "tetr/engine/movement_presentation.h"
#include "tetr/rules/spin.h"

#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
Replay::TetrioInputEncoder native_i_spin_fixture() {
    Runtime::Playfield field;
    field.initialize(10, 20);
    field.clear();
    const std::string rows[] = {"...j......", "...jjj..oo", ".....t..oo",
        ".....tt..j", "loot.tlllj", "####.#####"};
    for (int row = 14; row < 20; ++row) for (int col = 0; col < 10; ++col)
        if (rows[row - 14][col] != '.') field.set_cell(row, col, Runtime::BoardCell::garbage());
    const auto piece = Piece::get_definition(Piece::Type::from_index(0));
    Engine::MovementState state;
    state.pose = {{-2, 4}, Runtime::Rotation{0}};
    const auto can = [&](Piece::Pose pose) { return piece.can_place(pose, field); };
    const auto rotate = [&](Piece::Pose& pose, Runtime::Rotation rotation, Piece::KickIndex& kick) {
        return piece.try_rotate(pose, rotation, field, kick);
    };
    Replay::TetrioInputEncoder encoder;
    for (const char command : std::string("ZlsXZH")) {
        const auto before = state.pose;
        Engine::apply_movement_script_command(state,
            Engine::decode_movement_script_command(command), can, rotate);
        encoder.movement(command, state.pose.position.row - before.position.row,
                         state.pose.position.col - before.position.col);
    }
    assert(state.pose.position.row == 17 && state.pose.position.col == 4);
    assert(state.pose.rotation.index() == 3);
    return encoder;
}

Replay::TetrioInputEncoder native_spin_fixture() {
    Runtime::Playfield field;
    field.initialize(10, 20);
    field.clear();
    field.set_cell(17, 3, Runtime::BoardCell::garbage());
    for (int x = 0; x < 10; ++x) {
        if (x < 3 || x > 5) field.set_cell(18, x, Runtime::BoardCell::garbage());
        if (x != 4) field.set_cell(19, x, Runtime::BoardCell::garbage());
    }
    const auto piece = Piece::get_definition(Piece::Type::from_index(5)); // SRS+ T
    Engine::MovementState state;
    state.pose = {{17, 4}, Runtime::Rotation{1}};
    const auto can = [&](Piece::Pose pose) { return piece.can_place(pose, field); };
    const auto rotate = [&](Piece::Pose& pose, Runtime::Rotation rotation, Piece::KickIndex& kick) {
        return piece.try_rotate(pose, rotation, field, kick);
    };
    assert(can(state.pose));
    Replay::TetrioInputEncoder encoder;
    for (const char command : std::string("SXSH")) {
        const auto before = state.pose;
        Engine::apply_movement_script_command(state,
            Engine::decode_movement_script_command(command), can, rotate);
        encoder.movement(command, state.pose.position.row - before.position.row,
                         state.pose.position.col - before.position.col);
    }
    assert(state.pose.position.row == 18 && state.pose.position.col == 4);
    assert(state.pose.rotation.index() == 2);
    Rules::SpinInput input;
    input.spinChecker = Piece::SpinCheck::TCorner;
    input.pose = state.pose;
    input.lastRotation = state.lastRotation;
    input.mapWidth = 10;
    input.mapHeight = 20;
    const auto spin = Rules::detect_spin(input,
        [&](int row, int col) { return field.cell(row, col).occupied(); });
    assert(spin.spin && !spin.mini);
    for (const auto& cell : piece.cells(state.pose.rotation))
        field.set_cell(state.pose.position.row + cell[0], state.pose.position.col + cell[1],
                       Runtime::BoardCell::piece(piece.color_id()));
    for (int row : {18, 19}) for (int col = 0; col < 10; ++col)
        assert(field.cell(row, col).occupied());
    return encoder;
}

void check_pairs(const Replay::TetrioInputEncoder& encoder) {
    const auto& events = encoder.events();
    assert(events.size() % 2 == 0);
    double previous = -1;
    for (std::size_t i = 0; i < events.size(); i += 2) {
        const auto& down = events[i];
        const auto& up = events[i + 1];
        assert(down.down && !up.down && down.key == up.key && down.frame == up.frame);
        assert(down.subframe >= 0 && up.subframe < 1);
        assert(down.frame + down.subframe >= previous);
        assert((up.subframe > down.subframe) == (down.key == "softDrop"));
        previous = up.frame + up.subframe;
    }
}

void tests() {
    Replay::TetrioInputEncoder encoder;
    encoder.advance_to(3, 0.9999);
    encoder.movement('S', 1, 0);
    assert(encoder.events().size() == 2);
    assert(encoder.events()[0].frame == 4);
    encoder.movement('s', 18, 0);
    assert(encoder.events().size() == 38);
    encoder.movement('S', 0, 0);
    encoder.movement('X', -1, 1);
    encoder.movement('H', 0, 0);
    encoder.hold();
    encoder.movement('l', 0, -4);
    encoder.movement('r', 0, 8);
    const auto before = encoder.events().size();
    for (char c : std::string("Ssq")) {
        try { encoder.movement(c, -1, 0); assert(false); }
        catch (const std::invalid_argument&) {}
    }
    assert(encoder.events().size() == before);
    for (double t : {-1., 1., std::nan("")}) {
        try { encoder.advance_to(0, t); assert(false); }
        catch (const std::invalid_argument&) {}
    }
    for (int i = 0; i < 30000; ++i) encoder.movement('S', 1, 0);
    check_pairs(encoder);
    check_pairs(native_spin_fixture());
    check_pairs(native_i_spin_fixture());
}

void fixture(const std::string& name, const Replay::TetrioInputEncoder& encoder, bool comma) {
    check_pairs(encoder);
    std::cout << (comma ? "," : "") << "\"" << name << "\":[";
    bool first = true;
    for (const auto& event : encoder.events()) {
        std::cout << (first ? "" : ",") << "{\"frame\":" << event.frame
                  << ",\"type\":\"" << (event.down ? "keydown" : "keyup")
                  << "\",\"data\":{\"key\":\"" << event.key
                  << "\",\"subframe\":" << std::setprecision(12) << event.subframe << "}}";
        first = false;
    }
    std::cout << "]";
}
}

int main(int argc, char** argv) {
    tests();
    if (argc == 2 && std::string(argv[1]) == "--fixtures") {
        std::cout << "{";
        Replay::TetrioInputEncoder single;
        single.movement('S', 1, 0);
        fixture("single", single, false);
        Replay::TetrioInputEncoder boundary;
        boundary.advance_to(0, 0.9999);
        boundary.movement('S', 1, 0);
        fixture("boundary", boundary, true);
        Replay::TetrioInputEncoder floor;
        floor.movement('s', 21, 0);
        fixture("floor", floor, true);
        Replay::TetrioInputEncoder rotate;
        rotate.movement('S', 1, 0);
        rotate.movement('X', 0, 0);
        fixture("single_rotate", rotate, true);
        Replay::TetrioInputEncoder blocked;
        blocked.movement('S', 0, 0);
        fixture("blocked", blocked, true);
        const auto spin = native_spin_fixture();
        fixture("spin_double", spin, true);
        fixture("i_spin_double", native_i_spin_fixture(), true);
        std::cout << ",\"rotation_profile\":" << Replay::tetrio_native_rotation_profile();
        std::cout << "}\n";
    } else std::cout << "TETR.IO input encoder tests passed\n";
}
