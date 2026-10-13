#ifndef TETR_ENGINE_PLACEMENT_SIDE_EFFECTS_H
#define TETR_ENGINE_PLACEMENT_SIDE_EFFECTS_H

#include "tetr/engine/placement_application.h"
#include "tetr/rules/game_result.h"

namespace Runtime { class Playfield; class GameContext; struct PlayerSessionState; }
namespace Piece { class Definition; }
namespace Rules { struct PlacementResult; }

namespace Engine {

struct PlacementOffsetResolution {
    Rules::PlayResult signal = Rules::PlayResult::ContinueFrame;
    int rawOffsetResult = 0;
};

void apply_pre_offset_placement_garbage(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player,
    const PlacementPreOffsetEffects& effects);

PlacementOffsetResolution resolve_placement_offset(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    const PlacementPreOffsetEffects& effects, int perfectClearAttack = 0);

void apply_post_offset_placement_garbage(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player,
    const PlacementPostOffsetEffects& effects);

Rules::PlayResult complete_locked_piece_side_effects(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    const Rules::PlacementResult& placement,
    const PlacementPostOffsetEffects& effects);

} // namespace Engine

#endif
