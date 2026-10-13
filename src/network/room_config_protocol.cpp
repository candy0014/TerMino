#include "tetr/network/room_config_protocol.h"
#include "tetr/util/text_codec.h"
#include <algorithm>
namespace Network {
namespace {
bool token_valid(std::string_view token) {
    return !token.empty() && token.size()<=64 && token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")==std::string_view::npos;
}
}
std::uint64_t room_config_digest(std::string_view bytes) {
    std::uint64_t hash=14695981039346656037ULL;
    for (unsigned char c:bytes) {hash^=c; hash*=1099511628211ULL;}
    return hash;
}
std::size_t room_config_chunk_count(std::size_t bytes) {return (bytes+kRoomConfigChunkBytes-1)/kRoomConfigChunkBytes;}
std::string encode_config_message(const ConfigMessage& m) {
    std::string tag;
    switch (m.kind) {
        case ConfigMessageKind::Begin: tag="!config-begin"; break;
        case ConfigMessageKind::Part: tag="!config-part"; break;
        case ConfigMessageKind::Stored: tag="!config-stored"; break;
        case ConfigMessageKind::Received: tag="!config-received"; break;
        case ConfigMessageKind::Applied: tag="!config-applied"; break;
    }
    auto wire=tag+";"+m.token+";"+std::to_string(m.epoch)+";";
    if (m.kind==ConfigMessageKind::Begin)
        return wire+std::to_string(m.roomInstance)+";"+std::to_string(m.hostSlot)+";"+(m.policy.forceRules?"1":"0")+";"+(m.policy.forceHandling?"1":"0");
    if (m.kind==ConfigMessageKind::Part)
        return wire+std::to_string(m.bytes)+";"+std::to_string(m.digest)+";"+std::to_string(m.index)+";"+std::to_string(m.partDigest)+";"+m.data;
    return wire+std::to_string(m.digest)+";"+std::to_string(m.index);
}
std::optional<ConfigMessage> decode_config_message(std::string_view wire) {
    if (wire.size()>1300) return std::nullopt;
    std::vector<std::string_view> fields;
    std::size_t at=0;
    // A part's body can contain arbitrary separators and binary bytes.
    const int separators=wire.rfind("!config-part;",0)==0?7:6;
    for (int i=0;i<separators;++i) {
        const auto end=wire.find(';',at);
        if (end==std::string_view::npos) break;
        fields.push_back(wire.substr(at,end-at)); at=end+1;
    }
    fields.push_back(wire.substr(at));
    if (fields.size()<5 || !token_valid(fields[1])) return std::nullopt;
    const auto epoch=Util::Text::parse_integer<std::uint64_t>(fields[2]);
    if (!epoch || !*epoch) return std::nullopt;
    ConfigMessage m; m.token=std::string(fields[1]); m.epoch=*epoch;
    if (fields[0]=="!config-begin" && fields.size()==7) {
        const auto instance=Util::Text::parse_integer<std::uint64_t>(fields[3]);
        const auto slot=Util::Text::parse_integer<int>(fields[4]);
        if (!instance || !*instance || !slot || *slot<0 || *slot>255 || (fields[5]!="0" && fields[5]!="1") || (fields[6]!="0" && fields[6]!="1")) return std::nullopt;
        m.roomInstance=*instance; m.hostSlot=*slot; m.policy={fields[5]=="1",fields[6]=="1"}; return m;
    }
    if (fields[0]=="!config-part" && fields.size()==8) {
        const auto bytes=Util::Text::parse_integer<std::size_t>(fields[3]);
        const auto digest=Util::Text::parse_integer<std::uint64_t>(fields[4]);
        const auto index=Util::Text::parse_integer<std::size_t>(fields[5]);
        const auto partDigest=Util::Text::parse_integer<std::uint64_t>(fields[6]);
        if (!bytes || !*bytes || *bytes>kMaxRoomConfigBytes || !digest || !index || !partDigest || *index>=room_config_chunk_count(*bytes)) return std::nullopt;
        const auto expected=std::min(kRoomConfigChunkBytes,*bytes-*index*kRoomConfigChunkBytes);
        if (fields[7].size()!=expected || room_config_digest(fields[7])!=*partDigest) return std::nullopt;
        m.kind=ConfigMessageKind::Part; m.bytes=*bytes; m.digest=*digest; m.partDigest=*partDigest; m.index=*index; m.data=std::string(fields[7]); return m;
    }
    if (fields.size()!=5) return std::nullopt;
    if (fields[0]=="!config-stored") m.kind=ConfigMessageKind::Stored;
    else if (fields[0]=="!config-received") m.kind=ConfigMessageKind::Received;
    else if (fields[0]=="!config-applied") m.kind=ConfigMessageKind::Applied;
    else return std::nullopt;
    const auto digest=Util::Text::parse_integer<std::uint64_t>(fields[3]);
    const auto index=Util::Text::parse_integer<std::size_t>(fields[4]);
    if (!digest || !index || *index>=room_config_chunk_count(kMaxRoomConfigBytes)) return std::nullopt;
    m.digest=*digest; m.index=*index; return m;
}
ConfigMessage room_config_part(std::string_view payload,const std::string& token,std::uint64_t epoch,std::uint64_t digest,std::size_t index) {
    ConfigMessage m; m.kind=ConfigMessageKind::Part; m.token=token; m.epoch=epoch; m.digest=digest; m.bytes=payload.size(); m.index=index;
    m.data=std::string(payload.substr(index*kRoomConfigChunkBytes,kRoomConfigChunkBytes)); m.partDigest=room_config_digest(m.data); return m;
}
bool ConfigAssembler::insert(const ConfigMessage& part) {
    if (part.kind!=ConfigMessageKind::Part || !part.bytes || part.bytes>kMaxRoomConfigBytes ||
        part.index>=room_config_chunk_count(part.bytes) ||
        part.data.size()!=std::min(kRoomConfigChunkBytes,part.bytes-part.index*kRoomConfigChunkBytes) ||
        room_config_digest(part.data)!=part.partDigest) return false;
    if (!bytes) {
        bytes=part.bytes; digest=part.digest; parts.resize(room_config_chunk_count(bytes)); present.resize(parts.size());
    }
    if (bytes!=part.bytes || digest!=part.digest) return false;
    if (present[part.index]) return parts[part.index]==part.data;
    parts[part.index]=part.data; present[part.index]=true; ++received;
    if (received==parts.size()) {
        std::string assembled; assembled.reserve(bytes);
        for (const auto& chunk:parts) assembled+=chunk;
        if (room_config_digest(assembled)!=digest) { *this={}; return false; }
        payload=std::move(assembled);
    }
    return true;
}
}
