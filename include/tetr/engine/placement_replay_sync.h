#ifndef TETR_ENGINE_PLACEMENT_REPLAY_SYNC_H
#define TETR_ENGINE_PLACEMENT_REPLAY_SYNC_H

#include "tetr/rules/game_result.h"

namespace Runtime { class Playfield; class GameContext; struct PlayerSessionState; }
namespace Piece { class Definition; }

namespace Engine {

struct ReplayOffsetSyncResult {
    Rules::PlayResult signal = Rules::PlayResult::ContinueFrame;
    int offsetResult = 0;
};

ReplayOffsetSyncResult wait_for_replay_offset_data(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player);

// Consume replay side effects until end_place/end_all and restore the QP
// lock-state payload when present.
Rules::PlayResult wait_for_replay_lock_completion(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player);

} // namespace Engine

#endif
