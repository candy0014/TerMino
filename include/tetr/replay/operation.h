#ifndef TETR_REPLAY_OPERATION_H
#define TETR_REPLAY_OPERATION_H

#include "tetr/runtime/board_cell.h"
#include "tetr/runtime/strong_types.h"
#include "tetr/runtime/time.h"
#include "tetr/runtime/quick_play_progress.h"

#include <string>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace Replay {

enum class OperationKind {
    Unknown,
    PutErase,
    PutDraw,
    Hold,
    AddGarbage,
    AddSolidGarbage,
    AddBuffer,
    OffsetBuffer,
    StartPlace,
    Move,
    EndMove,
    EndMoveData,
    OffsetData,
    ClearLine,
    EndClearLine,
    EndPlaceData,
    EndPlace,
    EndAll,
    PauseGameTime,
    ResumeGameTime,
    RaiseBuffer,
    QuickPlayWindup
};

OperationKind operation_kind_from_name(std::string_view name);
std::string_view operation_name(OperationKind kind);

struct PosePayload {
    Piece::Pose pose{};
};

struct EndMoveDataPayload {
    Runtime::RotationRecord lastRotation{};
    long long score = 0;
    Piece::Pose pose{};
};

struct AddGarbagePayload {
    int height = 0;
    bool preserveHole = false;
    Runtime::BoardCell cell = Runtime::BoardCell::garbage();
    struct QuickPlayPolicy {
        int floor=1;
        int receiveProtection=0;
        double elapsedSeconds=0;
    };
    std::optional<QuickPlayPolicy> quickPlayPolicy;
};

struct AddSolidGarbagePayload {
    int height = 0;
    Runtime::BoardCell cell = Runtime::BoardCell::solid_garbage();
};

struct AddBufferPayload {
    int attack = 0;
    bool instant = false;
    int resolvedLines = -1;
    std::optional<double> incomingFraction;
    bool deferDisplay = false;
    std::optional<int> receivedAttack;
};

struct OffsetBufferPayload {
    int attack = 0;
    int clearedLines = 0;
    int perfectClearAttack = 0;
};

struct RaiseBufferPayload {
    int lines = 0;
};

struct QuickPlayWindupPayload {
    Runtime::QuickPlayWindupWarning warning;
};

struct OffsetDataPayload {
    int result = 0;
};

struct QuickPlayPlacementPayload {
    double speed = 0.0;
    double altitude = 0.0;
    int floor = 1;
    double garbageMultiple = 1.0;
    Runtime::Duration garbageInTime = Runtime::Duration::from_seconds(1.0);
    bool bagBreather = false;
    std::optional<Runtime::QuickPlayProgress> progress;
};

// Unknown records are the only decoded operations that keep historical text.
// Known gameplay records are converted to typed payloads at the codec boundary.
struct UnknownPayload {
    std::string name;
    std::vector<std::string> arguments;
};

using OperationPayload = std::variant<
    std::monostate,
    PosePayload,
    EndMoveDataPayload,
    AddGarbagePayload,
    AddSolidGarbagePayload,
    AddBufferPayload,
    OffsetBufferPayload,
    RaiseBufferPayload,
    QuickPlayWindupPayload,
    OffsetDataPayload,
    QuickPlayPlacementPayload,
    UnknownPayload>;

struct Operation {
    OperationKind kind = OperationKind::Unknown;
    Runtime::Duration timestamp{};
    OperationPayload payload{};
    // Separate game-rule time from the shared presentation timeline.
    std::optional<Runtime::Duration> gameTimestamp;

    template <class Payload>
    const Payload* payload_if() const noexcept {
        return std::get_if<Payload>(&payload);
    }

    template <class Payload>
    const Payload& payload_as() const {
        return std::get<Payload>(payload);
    }
};

} // namespace Replay

#endif
