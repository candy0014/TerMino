#ifndef TETR_ENGINE_LOCKED_PIECE_RESOLUTION_H
#define TETR_ENGINE_LOCKED_PIECE_RESOLUTION_H

#include "tetr/rules/placement_resolution.h"

namespace Runtime { class Playfield; class GameContext; struct PlayerSessionState; }
namespace Piece { class Definition; struct Pose; }
namespace Runtime { struct RotationRecord; }

namespace Engine {

// Commit the stopped piece to the field, derive immobility/spin inputs and run
// the pure placement rules. Presentation, replay and garbage side effects are
// intentionally outside this function.
Rules::PlacementResult resolve_locked_piece(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    Piece::Pose pose,
    Runtime::RotationRecord lastRotation);

} // namespace Engine

#endif
