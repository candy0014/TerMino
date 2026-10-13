#include "tetr/replay/movement_event.h"
#include "tetr/replay/operation_codec.h"

#include <utility>

namespace Replay {

MovementEventType movement_event_type(OperationKind kind) {
    switch (kind) {
        case OperationKind::StartPlace: return MovementEventType::StartPlace;
        case OperationKind::PutErase: return MovementEventType::Put0;
        case OperationKind::PutDraw: return MovementEventType::Put1;
        case OperationKind::Move: return MovementEventType::Move;
        case OperationKind::Hold: return MovementEventType::Hold;
        case OperationKind::EndMove: return MovementEventType::EndMove;
        case OperationKind::EndMoveData: return MovementEventType::EndMoveData;
        case OperationKind::EndAll: return MovementEventType::EndAll;
        default: return MovementEventType::Other;
    }
}

MovementEventType movement_event_type(const std::string& op) {
    return movement_event_type(operation_kind_from_name(op));
}

MovementEvent parse_movement_event(const Operation& operation) {
    MovementEvent result;
    result.type = movement_event_type(operation.kind);
    result.timestamp = operation.timestamp;

    if (result.type == MovementEventType::Put0 ||
        result.type == MovementEventType::Put1 ||
        result.type == MovementEventType::Move) {
        if (const auto* payload = operation.payload_if<PosePayload>()) result.pose = payload->pose;
    } else if (result.type == MovementEventType::EndMoveData) {
        if (const auto* payload = operation.payload_if<EndMoveDataPayload>()) {
            result.lastRotation = payload->lastRotation;
            result.score = payload->score;
            result.pose = payload->pose;
        }
    }
    return result;
}

bool event_due(const Operation& operation, Runtime::Duration elapsed) {
    return elapsed >= operation.timestamp;
}

bool pop_due(
    std::queue<Operation>& events,
    Runtime::Duration elapsed,
    QueuedEvent& out) {
    if (events.empty() || !event_due(events.front(), elapsed)) return false;
    out.operation = std::move(events.front());
    events.pop();
    out.movement = parse_movement_event(out.operation);
    return true;
}

} // namespace Replay
