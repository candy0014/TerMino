#ifndef TETR_NETWORK_HYBRID_SYNC_H
#define TETR_NETWORK_HYBRID_SYNC_H
#include "tetr/network/frame_packet.h"
#include "tetr/runtime/time.h"
#include <map>
#include <memory>

namespace Network {
// A short presentation buffer spaces bursty arrivals by the sender's 120 Hz
// frame stamps. Only opponent presentation is delayed; local input is not.
class RemoteBoardTimeline {
public:
    static constexpr double kBufferSeconds = 0.035;
    static constexpr std::size_t kMaxPending = 64;
    void reset(std::uint32_t epoch);
    void insert(FramePacket packet, Runtime::TimePoint receivedAt);
    std::optional<FramePacket> next(Runtime::TimePoint now);
    void invalidate_anchor() noexcept { hasAnchor_=false; needsSnapshot_=true; }
    bool needs_snapshot() const noexcept { return needsSnapshot_; }
    std::uint64_t applied_sequence() const noexcept { return appliedSequence_; }
    std::size_t pending_count() const noexcept { return pending_.size(); }
private:
    std::uint32_t epoch_=0;
    bool hasClock_=false, hasAnchor_=false, needsSnapshot_=false;
    std::uint64_t originFrame_=0, highestReceived_=0, appliedSequence_=0, appliedFrame_=0;
    std::uint64_t revision_=0, fingerprint_=0;
    Piece::Type piece_=Piece::Type::none();
    Runtime::TimePoint originTime_{};
    std::map<std::uint64_t,std::unique_ptr<FramePacket>> pending_;
    Runtime::TimePoint due_at(std::uint64_t frame) const;
};
struct HybridBoardSync {
    bool enabled=false;
    std::uint32_t epoch=0;
    std::uint64_t sentSequence=0, revision=0, fingerprint=0;
    std::optional<Runtime::TimePoint> origin;
    Runtime::TimePoint lastSnapshot{}, lastRequest{}, lastResponse{};
    bool requested=false, responded=false;
    Piece::Type piece=Piece::Type::none();
    std::optional<Piece::Pose> pose;
    std::optional<RemotePlacementSnapshot> lastPlacement;
    RemoteBoardTimeline incoming;
    std::unique_ptr<FramePacket> terminal;
    std::uint64_t snapshotsSent=0, motionsSent=0, snapshotBytes=0, motionBytes=0;
};
} // namespace Network
#endif
