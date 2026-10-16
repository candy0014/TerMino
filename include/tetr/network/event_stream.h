#ifndef TETR_NETWORK_EVENT_STREAM_H
#define TETR_NETWORK_EVENT_STREAM_H

#include "tetr/network/hybrid_protocol.h"
#include "tetr/network/board_view.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Network {
inline constexpr char kEventStreamProtocol[] = "termino.events.v1";
inline constexpr std::size_t kEventStreamMaxFrame = 65536;
inline constexpr std::size_t kEventStreamMaxPacket = 65507;
inline constexpr std::size_t kEventStreamMaxEvents = 64;

// Validate the complete envelope before exposing any events. Length prefixes
// preserve binary game packets, including NULs and delimiter bytes.
inline std::optional<std::vector<std::string_view>> decode_event_stream(std::string_view wire) {
    if (wire.size()<6 || wire.size()>kEventStreamMaxFrame || wire.substr(0,4)!="TES1") return {};
    const auto count=(static_cast<unsigned char>(wire[4])<<8)|static_cast<unsigned char>(wire[5]);
    if (!count || count>kEventStreamMaxEvents) return {};
    wire.remove_prefix(6);
    std::vector<std::string_view> events; events.reserve(count);
    for (int i=0;i<count;++i) {
        if (wire.size()<4) return {};
        std::uint32_t size=0;
        for (int byte=0;byte<4;++byte) size=(size<<8)|static_cast<unsigned char>(wire[byte]);
        wire.remove_prefix(4);
        if (size>kEventStreamMaxPacket || size>wire.size()) return {};
        events.push_back(wire.substr(0,size)); wire.remove_prefix(size);
    }
    if (!wire.empty()) return {};
    return events;
}
inline std::optional<std::string> encode_event_stream(const std::vector<std::string>& events) {
    if (events.empty() || events.size()>kEventStreamMaxEvents) return {};
    std::string wire="TES1";
    wire+=static_cast<char>(events.size()>>8); wire+=static_cast<char>(events.size());
    for (const auto& event:events) {
        if (event.size()>kEventStreamMaxPacket || wire.size()+4+event.size()>kEventStreamMaxFrame) return {};
        for (int byte=3;byte>=0;--byte) wire+=static_cast<char>((event.size()>>(byte*8))&255);
        wire+=event;
    }
    return wire;
}

// Resolved visual states are replaceable; checkpoints, attacks, controls and
// unknown extensions form barriers that must retain their relative order.
struct EventStreamVisual { std::string key; std::uint64_t sequence=0; bool independent=false; };
inline EventStreamVisual event_stream_visual(std::string_view wire) {
    std::string owner;
    int fields=wire.substr(0,7)=="!match;"?2:wire.substr(0,8)=="!player;"?3:0;
    if (fields) {
        std::size_t at=0;
        while (fields--) {
            const auto end=wire.find(';',at); if (end==wire.npos) return {};
            at=end+1;
        }
        owner=std::string(wire.substr(0,at)); wire.remove_prefix(at);
    }
    if (auto motion=decode_piece_motion(wire)) {
        const auto& stamp=motion->stamp;
        return {owner+"motion:"+std::to_string(stamp.epoch)+":"+std::to_string(stamp.revision)+":"+
            std::to_string(stamp.fingerprint)+":"+std::to_string(motion->piece.index()),stamp.sequence};
    }
    if (auto sequence=validated_mini_board_sequence(wire)) return {owner+"mini",*sequence,true};
    return {};
}

class EventStreamQueue {
public:
    using Clock=std::chrono::steady_clock;
    bool empty() const noexcept { return queue_.empty(); }
    std::size_t size() const noexcept { return queue_.size(); }
    std::size_t bytes() const noexcept { return bytes_; }
    bool push(std::string wire,Clock::time_point now,bool stream,
              std::size_t availableBytes=2*1024*1024,std::size_t maxEvents=1024) {
        if (wire.size()>kEventStreamMaxPacket) return false;
        const auto visual=stream?event_stream_visual(wire):EventStreamVisual{};
        if(visual.independent) {
            std::size_t removedBytes=0,removed=0;
            for(const auto& event:queue_)if(event.key==visual.key) {
                if(visual.sequence<event.sequence)return true;
                removedBytes+=event.wire.size();++removed;
            }
            if(bytes_-removedBytes+wire.size()>availableBytes || queue_.size()-removed>=maxEvents)return false;
            // A complete thumbnail supersedes its older images. Remove the
            // old images and append the new one after existing controls;
            // never move or discard a combat/control/checkpoint message.
            for(auto it=queue_.begin();it!=queue_.end();) {
                if(it->key==visual.key)it=queue_.erase(it);else ++it;
            }
            bytes_=bytes_-removedBytes+wire.size();
            queue_.push_back({std::move(wire),visual.key,now,visual.sequence});return true;
        }
        if (!visual.key.empty()) for (auto it=queue_.rbegin();it!=queue_.rend() && !it->key.empty();++it) {
            if (it->key!=visual.key) continue;
            if (visual.sequence<it->sequence) return true;
            if (bytes_-it->wire.size()+wire.size()>availableBytes) return false;
            bytes_=bytes_-it->wire.size()+wire.size(); it->wire=std::move(wire); it->sequence=visual.sequence;
            return true; // Preserve queue age while replacing unsent visual state.
        }
        if (bytes_+wire.size()>availableBytes || queue_.size()>=maxEvents) return false;
        bytes_+=wire.size(); queue_.push_back({std::move(wire),visual.key,now,visual.sequence}); return true;
    }
    Clock::duration oldest_age(Clock::time_point now) const {
        return queue_.empty()?Clock::duration::zero():std::max(Clock::duration::zero(),now-queue_.front().queuedAt);
    }
    // Flush whatever is already queued immediately. Coalescing is useful when
    // the socket is backpressured; it must not impose a batching delay at idle.
    std::vector<std::string> take(bool stream) {
        std::vector<std::string> events;
        std::size_t size=6;
        while (!queue_.empty() && events.size()<(stream?kEventStreamMaxEvents:1)) {
            if (stream && size+4+queue_.front().wire.size()>kEventStreamMaxFrame) break;
            size+=4+queue_.front().wire.size(); bytes_-=queue_.front().wire.size();
            events.push_back(std::move(queue_.front().wire)); queue_.pop_front();
        }
        return events;
    }
    std::string pop() {
        auto wire=std::move(queue_.front().wire); queue_.pop_front(); bytes_-=wire.size(); return wire;
    }
private:
    struct Event { std::string wire,key; Clock::time_point queuedAt; std::uint64_t sequence; };
    std::deque<Event> queue_;
    std::size_t bytes_=0;
};
} // namespace Network
#endif
