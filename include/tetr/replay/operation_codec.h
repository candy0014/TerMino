#ifndef TETR_REPLAY_OPERATION_CODEC_H
#define TETR_REPLAY_OPERATION_CODEC_H

#include "tetr/replay/operation.h"

#include <cstddef>
#include <queue>
#include <string>
#include <vector>

namespace Replay {

// Decode the historical token layout [operation, timestamp, arg0, ...].
// replayTimeMultiplier is the playback speed multiplier stored outside the
// replay stream; old playback divided each serialized timestamp by it.
Operation decode_operation_fields(
    const std::vector<std::string>& fields,
    double replayTimeMultiplier = 1.0);

// Decode every {...} replay operation at or after beginOffset. Configuration
// records before beginOffset remain owned by Init and are intentionally not
// interpreted here.
std::queue<Operation> decode_operation_stream(
    const std::string& replay,
    std::size_t beginOffset,
    double replayTimeMultiplier = 1.0);


std::string encode_operation_base(
    OperationKind operation,
    Runtime::Duration elapsed);

// String form is kept at the serialization boundary for unknown/historical
// records and focused codec tests.
std::string encode_operation_base(
    const std::string& operation,
    Runtime::Duration elapsed);

} // namespace Replay

#endif
