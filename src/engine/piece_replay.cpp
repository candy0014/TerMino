#include "piece_replay.h"

#include "tetr/replay/recorder.h"
#include "tetr/runtime/game_context.h"

namespace Engine {

void record_piece_pose(
    Runtime::GameContext& context,
    Replay::OperationKind operation,
    Piece::Pose pose) {
    Replay::record_pose_event(context, operation, pose);
}

} // namespace Engine
