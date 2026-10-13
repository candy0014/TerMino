#ifndef TETR_REPLAY_MOVEMENT_EVENT_H
#define TETR_REPLAY_MOVEMENT_EVENT_H

#include "tetr/replay/operation.h"
#include "tetr/runtime/strong_types.h"
#include "tetr/runtime/time.h"

#include <queue>
#include <string>

namespace Replay {

enum class MovementEventType { Other, StartPlace, Put0, Put1, Move, Hold, EndMove, EndMoveData, EndAll };

struct MovementEvent {
    MovementEventType type = MovementEventType::Other;
    Runtime::Duration timestamp{};
    Piece::Pose pose{};
    Runtime::RotationRecord lastRotation{};
    long long score = 0;
};

MovementEventType movement_event_type(OperationKind kind);
MovementEventType movement_event_type(const std::string& op);
MovementEvent parse_movement_event(const Operation& operation);
bool event_due(const Operation& operation, Runtime::Duration elapsed);
struct QueuedEvent { Operation operation; MovementEvent movement; };
bool pop_due(std::queue<Operation>& events, Runtime::Duration elapsed, QueuedEvent& out);

} // namespace Replay
#endif
