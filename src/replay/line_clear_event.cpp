#include "tetr/replay/line_clear_event.h"

#include "tetr/replay/movement_event.h"

#include <utility>

namespace Replay {

LineClearEventType line_clear_event_type(OperationKind kind) {
    switch (kind) {
        case OperationKind::ClearLine: return LineClearEventType::ClearLine;
        case OperationKind::EndClearLine: return LineClearEventType::EndClearLine;
        case OperationKind::EndAll: return LineClearEventType::EndAll;
        default: return LineClearEventType::Other;
    }
}

LineClearEventType line_clear_event_type(const std::string& op) {
    return line_clear_event_type(operation_kind_from_name(op));
}

bool is_line_clear_marker(
    const Operation& operation,
    LineClearMarker marker) {
    const auto type = line_clear_event_type(operation.kind);
    if (marker == LineClearMarker::ClearLine) {
        return type == LineClearEventType::ClearLine;
    }
    return type == LineClearEventType::EndClearLine;
}

LineClearPumpResult pump_due_until_line_clear_marker(
    std::queue<Operation>& events,
    Runtime::Duration elapsed,
    LineClearMarker marker,
    bool leaveMarkerQueued) {
    LineClearPumpResult result;
    while (!events.empty()) {
        if (leaveMarkerQueued && is_line_clear_marker(events.front(), marker)) {
            break;
        }
        if (!event_due(events.front(), elapsed)) break;

        Operation operation = std::move(events.front());
        events.pop();
        if (line_clear_event_type(operation.kind) == LineClearEventType::EndAll) {
            result.endAll = true;
            break;
        }
        const bool markerReached = is_line_clear_marker(operation, marker);
        result.events.push_back(std::move(operation));
        if (markerReached) {
            result.markerReached = true;
            break;
        }
    }
    return result;
}

} // namespace Replay
