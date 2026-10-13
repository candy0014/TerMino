#ifndef TETR_REPLAY_LINE_CLEAR_EVENT_H
#define TETR_REPLAY_LINE_CLEAR_EVENT_H

#include "tetr/replay/operation.h"
#include "tetr/runtime/time.h"

#include <queue>
#include <string>
#include <vector>

namespace Replay {

enum class LineClearEventType {
    Other,
    ClearLine,
    EndClearLine,
    EndAll
};

enum class LineClearMarker {
    ClearLine,
    EndClearLine
};

struct LineClearPumpResult {
    std::vector<Operation> events;
    bool markerReached = false;
    bool endAll = false;
};

LineClearEventType line_clear_event_type(OperationKind kind);
LineClearEventType line_clear_event_type(const std::string& op);
bool is_line_clear_marker(const Operation& operation, LineClearMarker marker);

// Consume every currently-due record up to a line-clear marker. With
// leaveMarkerQueued=true the target marker is deliberately left at the queue
// front even when already due; this reproduces the old animation loop, which
// reserved end_clear_line for the final post-animation wait.
LineClearPumpResult pump_due_until_line_clear_marker(
    std::queue<Operation>& events,
    Runtime::Duration elapsed,
    LineClearMarker marker,
    bool leaveMarkerQueued);

} // namespace Replay

#endif
