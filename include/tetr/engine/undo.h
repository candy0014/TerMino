#ifndef TETR_ENGINE_UNDO_H
#define TETR_ENGINE_UNDO_H

#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/movement_input_state.h"

#include <cstddef>

namespace Runtime { class GameContext; }

namespace Engine {

struct PieceLoopState {
    MovementInputState input;
    AutoplayCarryState autoplay;
    void reset() { input.clear(); autoplay={}; }
};

// Unwind the active piece/animation before replacing its referenced state.
struct UndoRequested {};

// Both frontends retain this many completed placements, plus the boundary
// snapshot needed to restore the oldest retained placement.
inline constexpr std::size_t kUndoPieceLimit = 256;

bool undo_enabled(const Runtime::GameContext& context) noexcept;
void begin_undo_history(Runtime::GameContext& context, PieceLoopState* loopState=nullptr);
void capture_undo_snapshot(Runtime::GameContext& context);
bool restore_previous_undo_snapshot(Runtime::GameContext& context);
void poll_undo_key(Runtime::GameContext& context);
std::size_t undo_snapshot_count(const Runtime::GameContext& context) noexcept;

} // namespace Engine

#endif
