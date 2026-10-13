#ifndef TETR_REPLAY_RECORDER_H
#define TETR_REPLAY_RECORDER_H

#include "tetr/replay/operation.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"

#include <string>

namespace Replay {

std::string replay_base(const Runtime::GameContext& context, OperationKind op);
// Boundary-only compatibility overload for unknown/test records.
std::string replay_base(const Runtime::GameContext& context, const std::string& op);
void append_record(Runtime::GameContext& context, const std::string& record);
// Add whole-attempt metadata to the file header without changing the live
// event stream or the byte offsets stored in undo snapshots.
std::string recording_for_save(const Runtime::GameContext& context);

void record_add_garbage(Runtime::GameContext& context, int height, bool preserveHole, Runtime::BoardCell cell);
void record_add_solid_garbage(Runtime::GameContext& context, int height, Runtime::BoardCell cell);
void record_add_buffer(Runtime::GameContext& context, int attack, bool instant, int resolvedLines,
                       double incomingFraction, bool deferDisplay = false, std::optional<int> receivedAttack = std::nullopt);
void record_pose_event(
    Runtime::GameContext& context,
    OperationKind operation,
    Piece::Pose pose);
void record_end_move_data(
    Runtime::GameContext& context,
    Runtime::RotationRecord lastRotation,
    long long score,
    Piece::Pose pose);
void record_quick_play_state(Runtime::GameContext& context, const Runtime::PlayerSessionState& player);

template<typename... T>
void add_replay(Runtime::GameContext& context, OperationKind op, const T&... args) {
    if (Replay::session(context).data.length() >= 10000000) return;
    std::string res = "{" + replay_base(context, op);
    if constexpr (sizeof...(args) > 0) {
        auto add = [&](const auto& x) { res += "," + std::to_string(x); };
        (add(args), ...);
    }
    res += "}";
    append_record(context, res);
}

// Kept for deliberately unknown records and serialization tests. Gameplay
// producers should use OperationKind so protocol spelling cannot leak inward.
template<typename... T>
void add_replay(Runtime::GameContext& context, const std::string& op, const T&... args) {
    if (Replay::session(context).data.length() >= 10000000) return;
    std::string res = "{" + replay_base(context, op);
    if constexpr (sizeof...(args) > 0) {
        auto add = [&](const auto& x) { res += "," + std::to_string(x); };
        (add(args), ...);
    }
    res += "}";
    append_record(context, res);
}

} // namespace Replay

#endif
