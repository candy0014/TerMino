#ifndef TETR_ENGINE_PLACEMENT_PRESENTATION_H
#define TETR_ENGINE_PLACEMENT_PRESENTATION_H

namespace Runtime { class Playfield; class GameContext; struct PlayerSessionState; }
namespace Piece { class Definition; }
namespace Rules { struct PlacementResult; }

namespace Engine {

void present_locked_piece_result(
    Runtime::GameContext& context,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    const Rules::PlacementResult& placement);

void present_post_offset_quick_play_state(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player,
    const Rules::PlacementResult& placement,
    bool decayTimedSolidGarbage);

void present_extended_all_spin_history(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    const Rules::PlacementResult& placement);

} // namespace Engine

#endif
