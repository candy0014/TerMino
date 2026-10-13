#ifndef TETR_ENGINE_LIVE_PIECE_PERIODIC_EVENTS_H
#define TETR_ENGINE_LIVE_PIECE_PERIODIC_EVENTS_H

#include "tetr/rules/game_result.h"

#include <functional>
#include <optional>

namespace Piece { class Definition; }
namespace Runtime {
class Playfield;
class GameContext;
struct PlayerSessionState;
}
namespace Engine { struct MovementState; }

namespace Engine {

std::optional<Rules::PlayResult> apply_immediate_garbage_to_live_piece(
    Runtime::GameContext& context, Runtime::Playfield& field, Piece::Definition& piece,
    Runtime::PlayerSessionState& player, MovementState& movementState,
    bool pieceVisible = true, bool* fieldChanged = nullptr);

std::optional<Rules::PlayResult> process_live_piece_periodic_events(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    MovementState& movementState,
    bool deferCheeseTimer,
    const std::function<Rules::PlayResult()>& pumpFrame,
    bool pieceVisible = true,
    bool* fieldChanged = nullptr);

void apply_due_cheese_timer_after_autoplay_lock(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player);

} // namespace Engine

#endif
