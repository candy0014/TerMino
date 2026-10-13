// C++ adaptation of MinusKelvin/cold-clear. SPDX-License-Identifier: MPL-2.0
#ifndef TETR_COLDCLEAR_CORE_H
#define TETR_COLDCLEAR_CORE_H

#include "tetr/bot/bot_coldclear.h"
#include "tetr/engine/movement_presentation.h"
#include "tetr/rules/placement_resolution.h"
#include <array>
#include <cstdint>
#include <deque>
#include <utility>

namespace ColdClear {
constexpr int Width = 10;
constexpr int Height = 20;
constexpr int Rows = Height + Runtime::kHiddenRowCapacity;
constexpr unsigned Full = (1u << Width) - 1;

// Bottom-up coordinates, as in upstream libtetris; poses retain host coordinates.
struct Board {
    std::array<std::uint16_t, Rows> rows{};
    std::array<std::uint16_t, Rows> pieceRows{};
    bool occupied(int x, int y) const noexcept;
    bool can_place(const Piece::Definition& piece, Piece::Pose pose) const;
    bool try_rotate(const Piece::Definition& piece, Piece::Pose& pose,
                    Runtime::Rotation target, Piece::KickIndex& kick) const;
    std::array<int, Width> heights() const;
    void place(const Piece::Definition& piece, Piece::Pose pose);
    void clear_lines();
};

struct State {
    Board board;
    int cursor = 0;
    int hold = -1;
    unsigned bag = 127; // bag at the visible queue endpoint, then speculation
    int b2b = 0;
    int combo = 0;
    int garbageHeight = 0;
    int placed = 0;
    int sent = 0;
    bool previousCleared = false;
    // A speculative piece consumed by empty HOLD must remain public in that
    // branch. The other choices share the same chance outcome.
    std::vector<int> tail;
    std::deque<Runtime::GarbagePacket> incoming;
    Runtime::GarbageCancellationState cancellation;
    double time = 0;
    int rise = 0; // unknown hole geometry: evaluate pressure and end this branch
};

struct Move {
    int piece = -1;
    Piece::Pose pose;
    Runtime::RotationRecord rotation;
    int time = 0;
    bool hold = false;
};

// Ports evaluation/standard.rs Value and Reward, including spike reset on a
// non-clearing placement and independent maxima during backpropagation.
struct Reward { int value = 0; int attack = -1; };
struct Value {
    int value = 0;
    int spike = 0;
    Value add(Reward reward) const {
        return {value + reward.value, reward.attack < 0 ? 0 : spike + reward.attack};
    }
    bool operator<(Value other) const {
        return value != other.value ? value < other.value : spike < other.spike;
    }
    bool operator==(Value other) const { return value == other.value && spike == other.spike; }
    void improve(Value other);
    std::int64_t weight(Value minimum, std::size_t rank) const;
};

struct Evaluation { Value value; Reward reward; };
struct Child {
    State state;
    Move move;
    Rules::PlacementResult lock;
    Evaluation evaluation;
};

struct Input {
    Bot::Environment environment;
    std::array<Piece::Definition, 7> pieces;
    std::array<Piece::Metadata, 7> metadata;
    std::vector<int> next;
    State root;
    Piece::Pose start;
    bool canHold = true;
};

std::vector<Move> find_moves(const Input& input, const State& state, int piece,
                            Piece::Pose start, bool hold);
Piece::Pose spawn(const Input& input, const State& state, int piece);
Child lock_move(const Input& input, State state, Move move);
// Terrain and T-slot evaluation are translated from upstream Standard.
int terrain(Board board, int b2b, int tCount);
Evaluation evaluate(const Input& input, const State& state, const Move& move,
                    const Rules::PlacementResult& lock);
Child search(const Input& input, Bot_coldclear::Diagnostics& diagnostics);
}
#endif
