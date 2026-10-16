#include "tetr/network/hybrid_sync.h"
#include <algorithm>
#include <iterator>

namespace Network {
void RemoteBoardTimeline::reset(std::uint32_t epoch) {
    *this=RemoteBoardTimeline{}; epoch_=epoch;
}
Runtime::TimePoint RemoteBoardTimeline::due_at(std::uint64_t frame) const {
    return originTime_+Runtime::Duration::from_seconds(
        (static_cast<double>(frame)-static_cast<double>(originFrame_))/kMotionFramesPerSecond+bufferSeconds_);
}
void RemoteBoardTimeline::insert(FramePacket packet,Runtime::TimePoint receivedAt) {
    if ((packet.checkpoint && (!packet.opponentState || packet.type!=FramePacketType::OpponentState)) ||
        (packet.motion && packet.type!=FramePacketType::OpponentMotion)) return;
    const auto* stamp=packet.checkpoint ? &*packet.checkpoint : packet.motion ? &packet.motion->stamp : nullptr;
    if (!stamp || stamp->epoch!=epoch_ || stamp->sequence<=appliedSequence_ || stamp->frame<appliedFrame_) return;
    if (!hasClock_) {
        hasClock_=true; originFrame_=stamp->frame; originTime_=receivedAt;
        lastClockSample_=receivedAt;
    } else if (stamp->sequence>highestReceived_) {
        if (stamp->frame<highestFrame_) return;
        const auto expected=originTime_+Runtime::Duration::from_seconds(
            (static_cast<double>(stamp->frame)-static_cast<double>(originFrame_))/kMotionFramesPerSecond);
        const double late=(receivedAt-expected).seconds();
        // The earliest observed transit is our local clock anchor. A late
        // burst must not restart the clock and add a new delay after a stall.
        if (late<0) originTime_+=Runtime::Duration::from_seconds(late);
        // Absorb short jitter quickly, then recover at 10ms per second. The
        // cushion cannot grow into seconds of buffered historical movement.
        const double decay=std::max(0.0,(receivedAt-lastClockSample_).seconds())*0.010;
        bufferSeconds_=std::clamp(std::max(std::max(0.0,late)+kMinBufferSeconds,
            bufferSeconds_-decay),kMinBufferSeconds,kMaxBufferSeconds);
        lastClockSample_=receivedAt;
    }
    if (stamp->sequence>highestReceived_) highestFrame_=stamp->frame;
    highestReceived_=std::max(highestReceived_,stamp->sequence);
    pending_.try_emplace(stamp->sequence,std::make_unique<FramePacket>(std::move(packet)));
    if (pending_.size()>kMaxPending) {
        // Keep a queued checkpoint when shedding replaceable motion. Dropping
        // the only anchor here would turn a traffic burst into a repair RTT.
        const auto motion=std::find_if(pending_.begin(),pending_.end(),[](const auto& item){return bool(item.second->motion);});
        if (motion!=pending_.end()) {pending_.erase(motion);++coalescedMotions_;}
        else {pending_.erase(pending_.begin());needsSnapshot_=true;}
    }
    if (!hasAnchor_ && std::none_of(pending_.begin(),pending_.end(),[](const auto& item){return bool(item.second->checkpoint);}))
        needsSnapshot_=true;
}
std::optional<FramePacket> RemoteBoardTimeline::next(Runtime::TimePoint now) {
    while (!pending_.empty()) {
        const auto& head=*pending_.begin()->second;
        const auto& stamp=head.checkpoint ? *head.checkpoint : head.motion->stamp;
        if (due_at(stamp.frame)>now) return std::nullopt;
        if (head.motion && (now-due_at(stamp.frame)).seconds()>kCatchupSeconds) {
            const auto following=std::next(pending_.begin());
            if (following!=pending_.end() && following->second->motion) {
                const auto& newer=*following->second->motion;
                if (newer.stamp.frame>=stamp.frame && newer.stamp.revision==stamp.revision && newer.stamp.fingerprint==stamp.fingerprint &&
                    newer.piece==head.motion->piece && due_at(newer.stamp.frame)<=now) {
                    pending_.erase(pending_.begin());++coalescedMotions_;continue;
                }
            }
        }
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
