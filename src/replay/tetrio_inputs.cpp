#include "tetr/replay/tetrio_inputs.h"
#include "tetr/runtime/piece.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <sstream>

namespace Replay {

std::string tetrio_native_rotation_profile() {
    static constexpr const char* transitions[] = {
        "01", "10", "12", "21", "23", "32", "30", "03", "02", "20", "13", "31"};
    std::ostringstream out;
    out << "{\"kicks\":";
    for (int family : {5, 0}) {
        if (family == 0) out << ",\"i_kicks\":";
        out << '{';
        const auto piece = Piece::get_definition(Piece::Type::from_index(family));
        for (int transition = 0; transition < 12; ++transition) {
            out << (transition ? "," : "") << '"' << transitions[transition] << "\":[";
            const auto& tests = piece.kick_tests()[transition];
            if (tests.empty() || tests.front() != Piece::Offset{0, 0})
                throw std::logic_error("Native rotation requires an initial no-kick test");
            // The client tests [0,0] implicitly; its Y axis points downward.
            for (std::size_t i = 1; i < tests.size(); ++i)
                out << (i > 1 ? "," : "") << '[' << tests[i][0] << ',' << -tests[i][1] << ']';
            out << ']';
        }
        out << '}';
    }
    out << ",\"additional_offsets\":{},\"spawn_rotation\":{},\"color_overrides\":{},\"preview_overrides\":{}}";
    return out.str();
}

void TetrioInputEncoder::advance_to(std::int64_t frame, double subframe) {
    if (frame < 0 || frame > (std::numeric_limits<std::int64_t>::max() - ticksPerFrame) / ticksPerFrame ||
        !std::isfinite(subframe) || subframe < 0 || subframe >= 1) {
        throw std::invalid_argument("Invalid TETR.IO input timestamp");
    }
    tick_ = std::max(tick_, frame * ticksPerFrame +
        static_cast<std::int64_t>(std::floor(subframe * ticksPerFrame)));
}

void TetrioInputEncoder::tap(const char* key, bool softDrop) {
    // Leave room for both edges in the SAME frame: at frame boundaries the
    // client performs another gravity update, which would add an unwanted cell.
    if (tick_ % ticksPerFrame >= ticksPerFrame - 2) {
        tick_ += ticksPerFrame - tick_ % ticksPerFrame;
    }
    const auto frame = tick_ / ticksPerFrame;
    const double at = double(tick_ % ticksPerFrame) / ticksPerFrame;
    events_.push_back({frame, at, key, true});
    events_.push_back({frame, at + (softDrop ? 1.0 / ticksPerFrame : 0.0), key, false});
    tick_ += 2;
}

void TetrioInputEncoder::hold() { tap("hold"); }

void TetrioInputEncoder::movement(char command, int rowsMoved, int columnsMoved) {
    // Validate before appending any events. Rotation can kick in either axis;
    // only translation commands have a constrained displacement.
    switch (command) {
        case 'S':
            if (rowsMoved < 0 || rowsMoved > 1 || columnsMoved != 0)
                throw std::invalid_argument("S must move zero or one row");
            // Also replay blocked input: it must not erase the last rotation.
            tap("softDrop", true);
            return;
        case 's':
            if (rowsMoved < 0 || rowsMoved > 200 || columnsMoved != 0)
                throw std::invalid_argument("Invalid floor soft-drop displacement");
            for (int i = 0; i < std::max(1, rowsMoved); ++i) tap("softDrop", true);
            return;
        case 'L': case 'l':
            if (rowsMoved != 0 || columnsMoved > 0 || columnsMoved < -100 ||
                (command == 'L' && columnsMoved < -1))
                throw std::invalid_argument("Invalid left movement displacement");
            for (int i = 0; i < std::max(1, -columnsMoved); ++i) tap("moveLeft");
            return;
        case 'R': case 'r':
            if (rowsMoved != 0 || columnsMoved < 0 || columnsMoved > 100 ||
                (command == 'R' && columnsMoved > 1))
                throw std::invalid_argument("Invalid right movement displacement");
            for (int i = 0; i < std::max(1, columnsMoved); ++i) tap("moveRight");
            return;
        case 'X': tap("rotateCW"); return;
        case 'Z': tap("rotateCCW"); return;
        case 'A': tap("rotate180"); return;
        case 'H': tap("hardDrop"); return;
        default: throw std::invalid_argument("Unknown movement script command");
    }
}

} // namespace Replay
