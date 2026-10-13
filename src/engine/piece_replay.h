#ifndef TETR_ENGINE_PIECE_REPLAY_H
#define TETR_ENGINE_PIECE_REPLAY_H

#include "tetr/replay/operation.h"
#include "tetr/runtime/piece.h"

namespace Runtime { class GameContext; }

namespace Engine {
void record_piece_pose(
    Runtime::GameContext& context,
    Replay::OperationKind operation,
    Piece::Pose pose);
}

#endif
