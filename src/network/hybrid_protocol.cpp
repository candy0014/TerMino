#include "tetr/network/hybrid_protocol.h"
#include "tetr/compat/piece_encoding.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/playfield.h"
#include "tetr/util/text_codec.h"
#include <array>
#include <limits>
#include <cstring>
#include <cmath>

namespace Network {
namespace {
std::string encode_stamp(const BoardFrameStamp& s) {
    return std::to_string(s.epoch)+";"+std::to_string(s.sequence)+";"+std::to_string(s.frame)+";"+
        std::to_string(s.revision)+";"+std::to_string(s.fingerprint);
}
std::optional<BoardFrameStamp> stamp_from(const std::array<std::string_view,5>& fields) {
    const auto epoch=Util::Text::parse_integer<std::uint32_t>(fields[0]);
    const auto seq=Util::Text::parse_integer<std::uint64_t>(fields[1]);
    const auto frame=Util::Text::parse_integer<std::uint32_t>(fields[2]);
    const auto revision=Util::Text::parse_integer<std::uint64_t>(fields[3]);
    const auto hash=Util::Text::parse_integer<std::uint64_t>(fields[4]);
    if (!epoch || !seq || !*seq || !frame || !revision || !*revision || !hash) return std::nullopt;
    return BoardFrameStamp{*epoch,*seq,*frame,*revision,*hash};
}
}
namespace {
void append_unsigned(std::string& out,std::uint64_t value) {
    do {
        const auto byte=static_cast<unsigned char>(value&127); value>>=7;
        out+=static_cast<char>(byte|(value ? 128 : 0));
    } while (value);
}
void append_signed(std::string& out,int value) {
    const auto encoded=value>=0 ? static_cast<std::uint64_t>(value)*2 :
        static_cast<std::uint64_t>(-static_cast<std::int64_t>(value))*2-1;
    append_unsigned(out,encoded);
}
void append_fixed(std::string& out,std::uint64_t value,int bytes) {
    for (int i=bytes-1;i>=0;--i) out+=static_cast<char>((value>>(i*8))&255);
}
std::optional<std::uint64_t> take_unsigned(std::string_view& wire) {
    std::uint64_t value=0;
    for (int i=0;i<10;++i) {
        if (wire.empty()) return std::nullopt;
        const auto byte=static_cast<unsigned char>(wire.front()); wire.remove_prefix(1);
        if (i==9 && (byte&254)) return std::nullopt;
        value|=static_cast<std::uint64_t>(byte&127)<<(i*7);
        if (!(byte&128)) {
            if (i && !byte) return std::nullopt; // Reject overlong encodings.
            return value;
        }
    }
    return std::nullopt;
}
std::optional<int> take_signed(std::string_view& wire) {
    const auto value=take_unsigned(wire);
    if (!value || *value>std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    const auto result=(*value&1) ? -static_cast<std::int64_t>(*value>>1)-1 : static_cast<std::int64_t>(*value>>1);
    return static_cast<int>(result);
}
std::optional<std::uint64_t> take_fixed(std::string_view& wire,int bytes) {
    if (wire.size()<static_cast<std::size_t>(bytes)) return std::nullopt;
    std::uint64_t value=0;
    for (int i=0;i<bytes;++i) value=(value<<8)|static_cast<unsigned char>(wire[i]);
    wire.remove_prefix(bytes); return value;
}
}
std::string encode_piece_motion(const PieceMotionFrame& m) {
    // WSS and UDP already carry binary messages. Fixed epoch/hash plus bounded
    // varints avoid repeating decimal IDs and hashes for every active frame.
    std::string wire="!motion-v1;";
    append_fixed(wire,m.stamp.epoch,4); append_unsigned(wire,m.stamp.sequence);
    append_unsigned(wire,m.stamp.frame); append_unsigned(wire,m.stamp.revision);
    append_fixed(wire,m.stamp.fingerprint,8); append_unsigned(wire,Compat::to_legacy(m.piece));
    append_unsigned(wire,static_cast<int>(m.kind));
    append_signed(wire,m.rows); append_signed(wire,m.columns); append_signed(wire,m.rotations);
    append_signed(wire,m.result.position.row); append_signed(wire,m.result.position.col);
    append_unsigned(wire,m.result.rotation.index());
    if(m.clock) {
        wire+='\1';
        std::uint64_t elapsed=0,rate=0;
        std::memcpy(&elapsed,&m.clock->elapsedSeconds,8);std::memcpy(&rate,&m.clock->rate,8);
        append_fixed(wire,elapsed,8);append_fixed(wire,rate,8);
    }
    return wire;
}
std::optional<PieceMotionFrame> decode_piece_motion(std::string_view wire) {
    constexpr std::string_view prefix="!motion-v1;";
    if (wire.size()>128 || wire.substr(0,prefix.size())!=prefix) return std::nullopt;
    wire.remove_prefix(prefix.size());
    const auto epoch=take_fixed(wire,4),seq=take_unsigned(wire),frame=take_unsigned(wire),revision=take_unsigned(wire),hash=take_fixed(wire,8);
    const auto piece=take_unsigned(wire),kind=take_unsigned(wire);
    const auto rows=take_signed(wire),cols=take_signed(wire),turns=take_signed(wire),row=take_signed(wire),col=take_signed(wire);
    const auto rotation=take_unsigned(wire);
    std::optional<RemoteClockSample> clock;
    if(!wire.empty()) {
        if(wire.size()!=17 || wire.front()!='\1') return std::nullopt;
        wire.remove_prefix(1);const auto elapsed=take_fixed(wire,8),rate=take_fixed(wire,8);
        RemoteClockSample value;
        std::memcpy(&value.elapsedSeconds,&*elapsed,8);std::memcpy(&value.rate,&*rate,8);
        if(!std::isfinite(value.elapsedSeconds) || value.elapsedSeconds<0 || value.elapsedSeconds>1e18 ||
           !std::isfinite(value.rate) || value.rate<0 || value.rate>1e18) return std::nullopt;
        clock=value;
    }
    if (!epoch || !seq || !*seq || !frame || *frame>std::numeric_limits<std::uint32_t>::max() ||
        !revision || !*revision || !hash || !piece || *piece>=Piece::catalog().size() || !kind || *kind>1 ||
        !rows || *rows < -400 || *rows>400 || !cols || *cols < -210 || *cols>210 ||
        !turns || *turns < -3 || *turns>3 || !row || *row < -210 || *row>210 || !col || *col < -105 || *col>210 ||
        !rotation || *rotation>3 || (*kind==0 && *turns!=0) || (*kind==1 && *turns==0)) return std::nullopt;
    return PieceMotionFrame{{static_cast<std::uint32_t>(*epoch),*seq,*frame,*revision,*hash},
        Compat::piece_type_from_legacy(static_cast<int>(*piece)),static_cast<PieceMotionKind>(*kind),
        *rows,*cols,*turns,{{*row,*col},Runtime::Rotation::from_index(static_cast<int>(*rotation))},clock};
}
std::string encode_board_checkpoint(const BoardFrameStamp& stamp,std::string_view state) {
    std::string packed="~";
    append_unsigned(packed,state.size());
    for (std::size_t at=0;at<state.size();) {
        auto end=at+1;
        while (end<state.size() && state[end]==state[at]) ++end;
        append_unsigned(packed,end-at); packed+=state[at]; at=end;
    }
    // Dense/alternating fields fall back to the original payload. Compression
    // therefore never makes a checkpoint payload larger.
    return "!snapshot-v1;"+encode_stamp(stamp)+";"+(packed.size()<state.size() ? packed : std::string(state));
}
std::optional<BoardCheckpointWire> decode_board_checkpoint(std::string_view wire) {
    if (wire.size()>60000) return std::nullopt;
    std::array<std::string_view,6> f{};
    for (auto& field:f) {
        const auto end=wire.find(';');
        if (end==std::string_view::npos) return std::nullopt;
        field=wire.substr(0,end); wire.remove_prefix(end+1);
    }
    if (f[0]!="!snapshot-v1" || wire.empty()) return std::nullopt;
    const auto stamp=stamp_from({f[1],f[2],f[3],f[4],f[5]});
    if (!stamp) return std::nullopt;
    std::string state;
    if (wire.front()=='~') {
        wire.remove_prefix(1);
        const auto size=take_unsigned(wire);
        if (!size || !*size || *size>60000) return std::nullopt;
        state.reserve(*size);
        while (!wire.empty()) {
            const auto count=take_unsigned(wire);
            if (!count || !*count || wire.empty() || *count>*size-state.size()) return std::nullopt;
            state.append(static_cast<std::size_t>(*count),wire.front()); wire.remove_prefix(1);
        }
        if (state.size()!=*size) return std::nullopt;
    } else state=std::string(wire);
    if (state.empty() || state.front()!='!') return std::nullopt;
    return BoardCheckpointWire{*stamp,std::move(state)};
}
std::string encode_snapshot_request(std::uint32_t epoch) { return "!snapshot-request;"+std::to_string(epoch); }
std::optional<std::uint32_t> decode_snapshot_request(std::string_view wire) {
    constexpr std::string_view prefix="!snapshot-request;";
    if (wire.substr(0,prefix.size())!=prefix) return std::nullopt;
    return Util::Text::parse_integer<std::uint32_t>(wire.substr(prefix.size()));
}
} // namespace Network
