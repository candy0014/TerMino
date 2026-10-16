#ifndef TETR_NETWORK_ROOM_CONFIG_PROTOCOL_H
#define TETR_NETWORK_ROOM_CONFIG_PROTOCOL_H
#include "tetr/network/lobby_protocol.h"
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace Network {
inline constexpr std::size_t kRoomConfigChunkBytes=1000;
inline constexpr std::size_t kMaxRoomConfigBytes=3100000;
enum class ConfigMessageKind { Begin, Part, Stored, Received, Applied };
struct ConfigMessage {
    ConfigMessageKind kind=ConfigMessageKind::Begin;
    std::string token;
    std::uint64_t epoch=0, digest=0, partDigest=0;
    std::uint64_t roomInstance=0;
    std::size_t bytes=0, index=0;
    int hostSlot=-1;
    RoomPolicy policy;
    std::string data;
    bool preview=false;
};
std::string encode_config_message(const ConfigMessage& message);
std::optional<ConfigMessage> decode_config_message(std::string_view wire);
std::uint64_t room_config_digest(std::string_view bytes);
std::size_t room_config_chunk_count(std::size_t bytes);
ConfigMessage room_config_part(std::string_view payload,const std::string& token,std::uint64_t epoch,std::uint64_t digest,std::size_t index);
struct ConfigAssembler {
    std::size_t bytes=0, received=0;
    std::uint64_t digest=0;
    std::vector<std::string> parts;
    std::vector<bool> present;
    std::optional<std::string> payload;
    bool insert(const ConfigMessage& part);
};
struct RoomConfigState {
    std::uint64_t epoch=0, uploadDigest=0;
    RoomPolicy policy;
    int hostSlot=-1;
    bool applied=false, finished=false;
    ConfigAssembler incoming;
    std::vector<bool> uploaded;
    std::size_t uploadCursor=0;
    std::chrono::steady_clock::time_point lastUpload{}, lastApplied{}, started{};
};
}
#endif
