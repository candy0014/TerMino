#ifndef TETR_RUNTIME_GARBAGE_PACKET_H
#define TETR_RUNTIME_GARBAGE_PACKET_H

#include "tetr/runtime/time.h"

namespace Runtime {

struct GarbageCancellationState {
    double cancellationFraction = 0.0;
    double outgoingFraction = 0.0;
};

// One pending incoming-garbage batch. Runtime keeps semantic state only;
// BOT adapters may project these typed fields into strategy-specific views at
// the call boundary; Runtime does not expose a legacy Board facade.
class GarbagePacket {
public:
    GarbagePacket() = default;
    GarbagePacket(int lineCount, TimePoint arrivalTime, bool preserveHole = false) noexcept
        : lineCount_(lineCount), arrivalTime_(arrivalTime), preserveHole_(preserveHole) {}

    int line_count() const noexcept { return lineCount_; }
    TimePoint arrival_time() const noexcept { return arrivalTime_; }
    bool preserve_hole() const noexcept { return preserveHole_; }

    void consume_lines(int lines) noexcept { lineCount_ -= lines; }
    void mark_preserve_hole() noexcept { preserveHole_ = true; }
    int source_slot() const noexcept {return sourceSlot_;}
    void set_source_slot(int slot) noexcept {sourceSlot_=slot;}

private:
    int lineCount_ = 0;
    TimePoint arrivalTime_{};
    bool preserveHole_ = false;
    int sourceSlot_ = -1;
};

} // namespace Runtime

#endif
