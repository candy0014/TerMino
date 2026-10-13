#include "tetr/network/hybrid_sync.h"
#include <cmath>

namespace Network {
void RemoteBoardTimeline::reset(std::uint32_t epoch) {
    *this=RemoteBoardTimeline{}; epoch_=epoch;
}
Runtime::TimePoint RemoteBoardTimeline::due_at(std::uint64_t frame) const {
    return originTime_+Runtime::Duration::from_seconds(
        (static_cast<double>(frame)-static_cast<double>(originFrame_))/kMotionFramesPerSecond+kBufferSeconds);
}
void RemoteBoardTimeline::insert(FramePacket packet,Runtime::TimePoint receivedAt) {
    if ((packet.checkpoint && (!packet.opponentState || packet.type!=FramePacketType::OpponentState)) ||
        (packet.motion && packet.type!=FramePacketType::OpponentMotion)) return;
    const auto* stamp=packet.checkpoint ? &*packet.checkpoint : packet.motion ? &packet.motion->stamp : nullptr;
    if (!stamp || stamp->epoch!=epoch_ || stamp->sequence<=appliedSequence_) return;
    if (!hasClock_ || (stamp->sequence>highestReceived_ &&
        std::abs((due_at(stamp->frame)-receivedAt).seconds())>0.25)) {
        hasClock_=true; originFrame_=stamp->frame; originTime_=receivedAt;
    }
    highestReceived_=std::max(highestReceived_,stamp->sequence);
    pending_.try_emplace(stamp->sequence,std::make_unique<FramePacket>(std::move(packet)));
    if (pending_.size()>kMaxPending) {
        pending_.erase(pending_.begin()); needsSnapshot_=true;
    }
}
std::optional<FramePacket> RemoteBoardTimeline::next(Runtime::TimePoint now) {
    while (!pending_.empty()) {
        const auto& head=*pending_.begin()->second;
        const auto& stamp=head.checkpoint ? *head.checkpoint : head.motion->stamp;
        if (due_at(stamp.frame)>now) return std::nullopt;
        auto packet=std::move(*pending_.begin()->second); pending_.erase(pending_.begin());
        const auto& s=packet.checkpoint ? *packet.checkpoint : packet.motion->stamp;
        if (s.sequence<=appliedSequence_ || s.frame<appliedFrame_) continue;
        if (packet.checkpoint && packet.opponentState) {
            hasAnchor_=true; needsSnapshot_=false;
            revision_=s.revision; fingerprint_=s.fingerprint; piece_=packet.opponentState->currentPiece;
        } else if (!hasAnchor_ || s.revision!=revision_ || s.fingerprint!=fingerprint_ || packet.motion->piece!=piece_) {
            if (!hasAnchor_ || s.revision>=revision_) needsSnapshot_=true;
            continue; // Do not speculate on a missing or different board.
        }
        appliedSequence_=s.sequence; appliedFrame_=s.frame;
        return packet;
    }
    return std::nullopt;
}
} // namespace Network
