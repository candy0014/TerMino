#ifndef TETR_ENGINE_PIECE_QUEUE_H
#define TETR_ENGINE_PIECE_QUEUE_H
#include "tetr/runtime/strong_types.h"
#include <vector>
namespace Runtime { class GameContext; struct PlayerSessionState; struct PieceQueueState; }
namespace Engine {
Runtime::PieceQueueState& piece_queue(Runtime::PlayerSessionState& player);
const Runtime::PieceQueueState& piece_queue(const Runtime::PlayerSessionState& player);
void add_bag(Runtime::GameContext& context,Runtime::PlayerSessionState& player);
Piece::Type current_piece(const Runtime::PlayerSessionState& player);
void consume_current_piece(Runtime::PlayerSessionState& player);
void prepend_piece(Runtime::PlayerSessionState& player,Piece::Type pieceType,bool startsBag=false);
bool finite_map_sequence(const Runtime::GameContext& context);
bool hold_replacement_available(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player);

// For a deterministic n-bag randomizer, return the set of pieces from the bag
// containing the last visible NEXT piece that are still hidden beyond the
// preview.  When NEXT count is zero, the current piece is the visible endpoint.
// The result is sorted by Piece::Type so callers learn membership only, not the
// hidden order.  Non n-bag randomizers return an empty vector.
std::vector<Piece::Type> unseen_pieces_in_preview_endpoint_bag(
    const Runtime::GameContext& context, const Runtime::PlayerSessionState& player);
}
#endif
