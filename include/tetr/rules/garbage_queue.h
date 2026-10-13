#ifndef TETR_RULES_GARBAGE_QUEUE_H
#define TETR_RULES_GARBAGE_QUEUE_H

#include "tetr/runtime/garbage_packet.h"
#include "tetr/runtime/time.h"

#include <deque>
#include <cstdint>
#include <vector>

namespace Rules {

using GarbagePacket = Runtime::GarbagePacket;

struct GarbageCancelInput {
    int attack = 0;
    int placedPieces = 0;
    bool multiplayerEnabled = false;
    int openerPhase = 14;
    int sentAttack = 0;
    bool legacyOpenerPhase = true;
    int perfectClearAttack = 0;
    double cancellationMultiplier = 1.0;
};

struct GarbageCancelResult {
    int outgoingAttack = 0;
    std::vector<int> outgoingPackets;
};

struct GarbageCancelAmountResult {
    std::int64_t remaining = 0;
    int outgoingAttack = 0;
};
GarbageCancelAmountResult cancel_garbage_amount(std::int64_t pending,int attack,double multiplier,
    bool doubled,Runtime::GarbageCancellationState& carry);

struct GarbageRiseChunk {
    int lines = 0;
    bool preserveHole = false;
};

struct GarbageRisePlan {
    std::vector<GarbageRiseChunk> chunks;
};

enum class GarbageBufferDisplayMode {
    Hidden,
    TwoStage,
    ThreeStage
};

int pending_garbage(const std::deque<GarbagePacket>& queue);

GarbagePacket make_garbage_packet(
    int attack,
    double garbageMultiple,
    Runtime::TimePoint now,
    bool instant,
    Runtime::Duration garbageInTime);

GarbageCancelResult cancel_incoming_garbage(
    std::deque<GarbagePacket>& queue,
    const GarbageCancelInput& input,
    Runtime::GarbageCancellationState* state = nullptr);

GarbageRisePlan consume_ready_garbage(
    std::deque<GarbagePacket>& queue,
    Runtime::TimePoint now,
    Runtime::Duration garbageInTime,
    int maximumRiseLines = 8);

std::vector<int> compute_garbage_buffer_display(
    const std::deque<GarbagePacket>& queue,
    Runtime::TimePoint now,
    double timeMultiplier,
    Runtime::Duration garbageInTime,
    GarbageBufferDisplayMode mode,
    int displaySize);

} // namespace Rules

#endif
