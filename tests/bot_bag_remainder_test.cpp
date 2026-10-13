#include "tetr/engine/piece_queue.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/runtime_services.h"
#include "tetr/runtime/session_state.h"

#include <algorithm>
#include <cassert>
#include <vector>

namespace {

std::vector<int> ids(const std::vector<Piece::Type>& pieces) {
    std::vector<int> out;
    out.reserve(pieces.size());
    for (Piece::Type piece : pieces) out.push_back(piece.index());
    return out;
}

void set_queue(Runtime::PlayerSessionState& player,
               std::initializer_list<Runtime::QueuedPiece> pieces,
               int nextCount) {
    player.pieces.bag.assign(pieces.begin(), pieces.end());
    player.gameplay.playfield.nextCount = nextCount;
}

} // namespace

int main() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session, services);
    auto& player = session.player;

    context.config.randomPieces = false;
    context.config.quickPlay.fool = Runtime::FoolMode::Disabled;

    // Standard n-bag semantics.  The visible endpoint is queue[nextCount].
    // Here current=0, NEXT={1,2}; the target bag still hides {3,4}.
    set_queue(player,
              {{Piece::Type{0}, true},
               {Piece::Type{1}, false},
               {Piece::Type{2}, false},
               {Piece::Type{4}, false},
               {Piece::Type{3}, false},
               {Piece::Type{5}, true},
               {Piece::Type{6}, false}},
              2);
    assert(ids(Engine::unseen_pieces_in_preview_endpoint_bag(context, player)) ==
           std::vector<int>({3, 4}));

    // NEXT=0: the current piece is the visible endpoint, so only later members
    // of that same bag are returned.
    set_queue(player,
              {{Piece::Type{4}, true},
               {Piece::Type{1}, false},
               {Piece::Type{3}, false},
               {Piece::Type{0}, true}},
              0);
    assert(ids(Engine::unseen_pieces_in_preview_endpoint_bag(context, player)) ==
           std::vector<int>({1, 3}));

    // A 5-piece bag works identically.  If NEXT reaches the end of that bag,
    // there is no hidden member to expose even though another bag follows.
    set_queue(player,
              {{Piece::Type{0}, true},
               {Piece::Type{1}, false},
               {Piece::Type{2}, false},
               {Piece::Type{3}, false},
               {Piece::Type{4}, false},
               {Piece::Type{4}, true},
               {Piece::Type{3}, false}},
              4);
    assert(Engine::unseen_pieces_in_preview_endpoint_bag(context, player).empty());

    // A 6-piece bag whose preview crosses into the next bag returns only the
    // suffix of the bag containing the final NEXT piece.
    set_queue(player,
              {{Piece::Type{0}, true},
               {Piece::Type{1}, false},
               {Piece::Type{2}, false},
               {Piece::Type{3}, false},
               {Piece::Type{4}, false},
               {Piece::Type{5}, false},
               {Piece::Type{5}, true},
               {Piece::Type{2}, false},
               {Piece::Type{4}, false},
               {Piece::Type{1}, false}},
              7);
    assert(ids(Engine::unseen_pieces_in_preview_endpoint_bag(context, player)) ==
           std::vector<int>({1, 4}));

    // Pentomino/Hexomino fool modes still use deterministic n-bag generation
    // over their allowed piece catalog, so the metadata remains valid.
    context.config.quickPlay.fool = Runtime::FoolMode::Pentomino;
    set_queue(player,
              {{Piece::Type{10}, true},
               {Piece::Type{11}, false},
               {Piece::Type{12}, false},
               {Piece::Type{13}, false}},
              1);
    assert(ids(Engine::unseen_pieces_in_preview_endpoint_bag(context, player)) ==
           std::vector<int>({12, 13}));
    context.config.quickPlay.fool = Runtime::FoolMode::Hexomino;
    assert(ids(Engine::unseen_pieces_in_preview_endpoint_bag(context, player)) ==
           std::vector<int>({12, 13}));
    context.config.quickPlay.fool = Runtime::FoolMode::Disabled;

    // Computing BOT metadata must not mutate queue contents or consume RNG.
    const auto queueBefore = player.pieces.bag;
    const auto rngBefore = player.pieces.random;
    (void)Engine::unseen_pieces_in_preview_endpoint_bag(context, player);
    assert(player.pieces.bag == queueBefore);
    assert(player.pieces.random == rngBefore);

    // With-replacement randomization is not an n-bag, so metadata is invalid.
    context.config.randomPieces = true;
    assert(Engine::unseen_pieces_in_preview_endpoint_bag(context, player).empty());
    context.config.randomPieces = false;

    // The 2026 Fool randomizer changes bag composition dynamically and is also
    // intentionally treated as non-n-bag metadata.
    context.config.quickPlay.fool = Runtime::FoolMode::AprilFools2026;
    assert(Engine::unseen_pieces_in_preview_endpoint_bag(context, player).empty());
    context.config.quickPlay.fool = Runtime::FoolMode::ReverseAprilFools2026;
    assert(Engine::unseen_pieces_in_preview_endpoint_bag(context, player).empty());

    return 0;
}
