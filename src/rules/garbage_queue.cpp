#include "tetr/rules/garbage_queue.h"
#include "tetr/rules/opener_phase.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Rules {

int pending_garbage(const std::deque<GarbagePacket>& queue) {
    std::int64_t total = 0;
    for (const auto& packet : queue) total += packet.line_count();
    return static_cast<int>(std::min<std::int64_t>(total,std::numeric_limits<int>::max()));
}

GarbagePacket make_garbage_packet(
    int attack,
    double garbageMultiple,
    Runtime::TimePoint now,
    bool instant,
    Runtime::Duration garbageInTime) {
    const Runtime::TimePoint timestamp = instant
        ? now - (garbageInTime + Runtime::Duration::from_seconds(1.0))
        : now;
    const double scaled=static_cast<double>(std::max(0,attack))*std::max(0.0,garbageMultiple);
    // Legacy recordings also pass through this path. Avoid an out-of-range
    // floating-to-int conversion when an attack or multiplier is very large.
    const int lines=std::isnan(scaled) ? 0 : scaled>=std::numeric_limits<int>::max()
        ? std::numeric_limits<int>::max() : static_cast<int>(scaled);
    return GarbagePacket{
        lines,
        timestamp,
        false
    };
}

GarbageCancelAmountResult cancel_garbage_amount(std::int64_t pending,int attack,double multiplier,
    bool doubled,Runtime::GarbageCancellationState& carry) {
    attack=std::max(0,attack);
    pending=std::max<std::int64_t>(0,pending);
    const auto before=pending;
    multiplier=std::isfinite(multiplier) ? std::clamp(multiplier,0.0,1000.0) : 1.0;
    double outgoing=attack;
    if (multiplier>0.0) {
        const double credit=carry.cancellationFraction;
        const double capacity=attack*multiplier*(doubled ? 2.0 : 1.0)+credit;
        pending-=std::min(pending,static_cast<std::int64_t>(std::floor(capacity+1e-9)));
        if (!pending) {
            outgoing=std::max(0.0,attack-std::max(0.0,before-credit)/multiplier);
            carry.cancellationFraction=0;
        } else {
            outgoing=0;
            carry.cancellationFraction=std::max(0.0,capacity-std::floor(capacity+1e-9));
        }
    } else {
        carry.cancellationFraction=0;
    }
    const double exact=outgoing+carry.outgoingFraction;
    const int whole=static_cast<int>(std::min<double>(std::floor(exact+1e-9),std::numeric_limits<int>::max()));
    carry.outgoingFraction=std::max(0.0,exact-whole);
    return {pending,whole};
}

GarbageCancelResult cancel_incoming_garbage(
    std::deque<GarbagePacket>& queue,
    const GarbageCancelInput& input,
    Runtime::GarbageCancellationState* state) {
    GarbageCancelResult result;
    Runtime::GarbageCancellationState temporary;
    auto& carry = state ? *state : temporary;
    int sent = input.sentAttack;
    const auto cancel = [&](int amount) {
        if (amount <= 0) return;
        std::int64_t pending = 0;
        for (const auto& packet : queue) pending += packet.line_count();
        const bool doubled = opener_phase_double_cancel(input.placedPieces,amount,
            static_cast<int>(std::min<std::int64_t>(pending,std::numeric_limits<int>::max())),
            input.multiplayerEnabled,input.openerPhase,sent,input.legacyOpenerPhase);
        const double multiplier=std::isfinite(input.cancellationMultiplier) ? std::clamp(input.cancellationMultiplier,0.0,1000.0) : 1.0;
        auto budget=static_cast<std::int64_t>(std::floor(amount*multiplier*(doubled ? 2.0 : 1.0)+
            (multiplier>0 ? carry.cancellationFraction : 0.0)+1e-9));
        const auto cancelled=cancel_garbage_amount(pending,amount,multiplier,doubled,carry);
        while (!queue.empty() && budget>0) {
            if (queue.front().line_count()<=budget) { budget-=queue.front().line_count(); queue.pop_front(); }
            else { queue.front().consume_lines(static_cast<int>(budget)); budget=0; }
        }
        const int whole=cancelled.outgoingAttack;
        result.outgoingAttack += whole;
        if (!input.legacyOpenerPhase && whole) result.outgoingPackets.push_back(whole);
        sent = static_cast<int>(std::min<std::int64_t>(static_cast<std::int64_t>(sent)+whole,
            std::numeric_limits<int>::max()));
    };
    const int attack = std::max(0,input.attack);
    if (input.legacyOpenerPhase) cancel(attack);
    else {
        // Ordinary clear and perfect-clear bonus are separate cancellation calls.
        const int pc = std::clamp(input.perfectClearAttack,0,attack);
        cancel(attack-pc);
        cancel(pc);
    }
    return result;
}

GarbageRisePlan consume_ready_garbage(
    std::deque<GarbagePacket>& queue,
    Runtime::TimePoint now,
    Runtime::Duration garbageInTime,
    int maximumRiseLines) {
    GarbageRisePlan result;
    int remaining = std::max(0, maximumRiseLines);

    while (!queue.empty() &&
           now - queue.front().arrival_time() >= garbageInTime &&
           remaining > 0) {
        if (queue.front().line_count() <= remaining) {
            const GarbagePacket packet = queue.front();
            queue.pop_front();
            result.chunks.push_back(GarbageRiseChunk{
                packet.line_count(),
                packet.preserve_hole(),packet.source_slot()
            });
            remaining -= packet.line_count();
        } else {
            const GarbagePacket packet = queue.front();
            queue.front().consume_lines(remaining);
            queue.front().mark_preserve_hole();
            result.chunks.push_back(GarbageRiseChunk{
                remaining,
                packet.preserve_hole(),packet.source_slot()
            });
            remaining = 0;
        }
    }
    return result;
}

std::vector<int> compute_garbage_buffer_display(
    const std::deque<GarbagePacket>& queue,
    Runtime::TimePoint now,
    double timeMultiplier,
    Runtime::Duration garbageInTime,
    GarbageBufferDisplayMode mode,
    int displaySize) {
    std::vector<int> states(static_cast<std::size_t>(std::max(0, displaySize)), 0);
    if (mode == GarbageBufferDisplayMode::Hidden || states.size() < 2) return states;
    const bool shortDelay = garbageInTime <= Runtime::Duration::from_seconds(0.3);
    const int lastIndex = static_cast<int>(states.size()) - 1;
    int offset = 0;

    for (const auto& packet : queue) {
        if (offset == lastIndex) break;
        if (packet.line_count() <= 0) continue;
        const int shownLines = std::min(packet.line_count(), lastIndex - offset);
        const Runtime::Duration age = (now - packet.arrival_time()) * timeMultiplier;
        const int state = mode == GarbageBufferDisplayMode::TwoStage
            ? (age < garbageInTime ? 1 : 2)
            : (shortDelay ? (age < garbageInTime ? 2 : 3)
                : (age < garbageInTime / 2.0 ? 1 : age < garbageInTime ? 2 : 3));
        std::fill(states.begin() + offset + 1, states.begin() + offset + shownLines + 1, state);
        offset += shownLines;
    }
    return states;
}

} // namespace Rules
