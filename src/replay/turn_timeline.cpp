#include "tetr/replay/turn_timeline.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/piece_turn.h"

#include <algorithm>
#include <array>
#include <vector>

namespace Replay {
namespace {
// Old simulated replays have no physical timestamps. Reconstruct their turn
// order without iterating empty quota rounds; movement timing is approximate.
void reconstruct_legacy_timeline(Runtime::GameContext& first, Runtime::GameContext& second) {
    std::array<Runtime::GameContext*, 2> contexts{&first, &second};
    std::queue<Operation> input[2]{std::move(session(first).operations),
                                 std::move(session(second).operations)};
    std::vector<Operation> output[2];
    std::uint64_t pieces[2]{};
    double cursor = 0.0;
    while (!input[0].empty() || !input[1].empty()) {
        const auto a = Runtime::completed_piece_turns(pieces[0], first.config.turnPieces);
        const auto b = Runtime::completed_piece_turns(pieces[1], second.config.turnPieces);
        const int side = input[0].empty() ? 1 : input[1].empty() ? 0 : a <= b ? 0 : 1;
        auto& config = contexts[side]->config;
        std::vector<Operation> chunk;
        bool complete = false;
        while (!input[side].empty()) {
            chunk.push_back(std::move(input[side].front()));
            input[side].pop();
            complete = chunk.back().kind == OperationKind::EndPlace;
            if (complete || chunk.back().kind == OperationKind::EndAll) break;
        }
        const bool hasPiece = std::any_of(chunk.begin(), chunk.end(), [](const Operation& op) {
            return op.kind == OperationKind::StartPlace;
        });
        const double rate = Runtime::autoplay_enabled(config.autoplay.mode) && config.autoplay.pps > 0
            ? config.autoplay.pps : config.simulationPps;
        const double movementDuration = hasPiece ? 1.0 / std::max(0.01, rate) : 0.0;
        std::size_t movementEnd = 0;
        for (std::size_t i = 0; i < chunk.size(); ++i)
            if (chunk[i].kind == OperationKind::EndMoveData) movementEnd = i;
        if (hasPiece && movementEnd == 0) movementEnd = chunk.size() - 1;
        bool cleared = false;
        for (std::size_t i = 0; i < chunk.size(); ++i) {
            auto& op = chunk[i];
            op.gameTimestamp = op.timestamp;
            if (op.kind == OperationKind::EndClearLine) cleared = true;
            const double movement = movementEnd == 0 ? 0.0
                : movementDuration * std::min(1.0, static_cast<double>(i) / movementEnd);
            op.timestamp = Runtime::Duration::from_seconds(cursor + movement +
                (cleared && hasPiece ? config.field.lineClearDelay.seconds() : 0.0));
            output[side].push_back(std::move(op));
        }
        if (hasPiece) cursor = output[side].back().timestamp.seconds();
        if (complete) ++pieces[side];
    }
    for (int side = 0; side < 2; ++side) {
        auto& state = session(*contexts[side]);
        state.operations = {};
        for (auto& op : output[side]) {
            if (op.kind == OperationKind::EndAll)
                op.timestamp = Runtime::Duration::from_seconds(cursor);
            state.operations.push(std::move(op));
        }
        state.separateGameTime = true;
        state.playback.duration = Runtime::Duration::from_seconds(cursor);
    }
}
} // namespace

void configure_turn_playback(Runtime::GameContext& first, Runtime::GameContext& second) {
    if (first.config.turnPieces <= 0 || second.config.turnPieces <= 0) return;
    if (!(session(first).separateGameTime && session(second).separateGameTime)) {
        // Before wall-time recording, zero PPS meant simultaneous play.
        if (first.config.simulationPps <= 0 || second.config.simulationPps <= 0) return;
        reconstruct_legacy_timeline(first, second);
    }
    auto turns = std::make_shared<TurnPlaybackState>();
    turns->quotas[0] = first.config.turnPieces;
    turns->quotas[1] = second.config.turnPieces;
    session(first).turnPlayback = turns;
    session(second).turnPlayback = turns;
    session(first).turnPlayerIndex = 0;
    session(second).turnPlayerIndex = 1;
}

bool can_start_turn(const Runtime::GameContext& context) {
    const auto& state = session(context);
    if (!state.turnPlayback) return true;
    const auto& turns = *state.turnPlayback;
    const int side = state.turnPlayerIndex;
    if (turns.ended[1 - side]) return true;
    const auto own = Runtime::completed_piece_turns(turns.completedPieces[side], turns.quotas[side]);
    const auto peer = Runtime::completed_piece_turns(turns.completedPieces[1-side], turns.quotas[1-side]);
    return side == 0 ? peer >= own : peer > own;
}

void mark_operation_consumed(Runtime::GameContext& context, const Operation& operation) {
    auto& state = session(context);
    state.lastConsumedTimestamp = operation.timestamp;
    state.lastGameTimestamp = operation.gameTimestamp.value_or(operation.timestamp);
    if (operation.kind == OperationKind::PauseGameTime) state.gameClockPaused = true;
    if (operation.kind == OperationKind::ResumeGameTime) state.gameClockPaused = false;
    if (state.turnPlayback) {
        if (operation.kind == OperationKind::EndPlace)
            ++state.turnPlayback->completedPieces[state.turnPlayerIndex];
        if (operation.kind == OperationKind::EndAll)
            state.turnPlayback->ended[state.turnPlayerIndex] = true;
    }
}

void finish_turn_playback(Runtime::GameContext& context) {
    auto& state = session(context);
    if (state.turnPlayback) state.turnPlayback->ended[state.turnPlayerIndex] = true;
}
} // namespace Replay
