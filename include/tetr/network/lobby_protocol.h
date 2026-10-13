#ifndef TETR_NETWORK_LOBBY_PROTOCOL_H
#define TETR_NETWORK_LOBBY_PROTOCOL_H

#include "tetr/runtime/strong_types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace Network {

inline constexpr std::size_t kMaxRoomNameBytes = 48;
std::string normalize_room_name(std::string_view name);
bool valid_room_name(std::string_view name, bool allowEmpty = false);
enum class RoomOperation { Create, Join, List, Leave, TransferHost, SetPolicy };
enum class RoomResult { Created, Joined, Listed, Left, NameTaken, NotFound, Full, InvalidName, AlreadyInRoom, Busy,
    HostTransferred, PolicyChanged, HostOnly, InvalidPlayer, RoundBusy };
struct RoomPolicy {
    bool forceRules = true;
    bool forceHandling = false;
    bool operator==(const RoomPolicy& other) const { return forceRules==other.forceRules && forceHandling==other.forceHandling; }
    bool operator!=(const RoomPolicy& other) const { return !(*this==other); }
};
struct RoomInfo {
    std::string name;
    bool isPublic = true;
    int players = 0;
    int capacity = 2;
};
struct RoomMember {
    int slot = -1;
    std::string username;
    bool host = false;
};
struct UsernameUpdate {
    std::string token, username;
    std::uint64_t revision = 1;
};
std::string encode_username_update(const UsernameUpdate& update, bool reply = false);
std::optional<UsernameUpdate> decode_username_update(std::string_view wire, bool reply = false);
struct RoomRequest {
    std::string token;
    std::uint64_t id = 0;
    RoomOperation operation = RoomOperation::List;
    std::string name;
    bool isPublic = true;
    int targetSlot = -1;
    RoomPolicy policy;
};
struct RoomResponse {
    std::string token;
    std::uint64_t id = 0; // Zero denotes a server membership update.
    RoomResult result = RoomResult::Listed;
    RoomInfo room;
    int playerIndex = -1;
    std::vector<RoomInfo> rooms;
    std::uint64_t stateRevision = 0;
    std::vector<RoomMember> members;
    RoomPolicy policy;
    bool hasPolicy = false;
    std::uint64_t roomInstance = 0;
};
std::string encode_room_request(const RoomRequest& request);
std::optional<RoomRequest> decode_room_request(std::string_view wire);
std::string encode_room_response(const RoomResponse& response);
std::optional<RoomResponse> decode_room_response(std::string_view wire);
std::string_view room_result_message(RoomResult result);

enum class ServerLobbySignal {
    PairReady,
    ConfigExchangeReady,
    CountdownReady,
    RoundReady
};

enum class ClientLobbySignal {
    ReadyForConfig,
    SetupReceived,
    CountdownComplete,
    Cancel
};

std::string_view encode_server_lobby_signal(ServerLobbySignal signal) noexcept;
std::optional<ServerLobbySignal> decode_server_lobby_signal(std::string_view wire) noexcept;
std::string_view encode_client_lobby_signal(ClientLobbySignal signal) noexcept;
std::optional<ClientLobbySignal> decode_client_lobby_signal(std::string_view wire) noexcept;

struct LobbyPlayerSetup {
    int visibleHeight = 0;
    int width = 0;
    int nextCount = 0;
    bool holdEnabled = false;
    std::vector<Piece::Type> nextPreview;
    int wins = 0;
    int blockMaxHeight = 0;
    int blockMaxWidth = 0;
    double simulationPps = 0.0;
    double turnPieces = 0.0;
    // Server-assigned slot of the player described by this setup.
    int playerIndex = -1;
    bool hybridBoardSync = false;
};

std::string encode_lobby_player_setup(const LobbyPlayerSetup& setup);
std::optional<LobbyPlayerSetup> decode_lobby_player_setup(std::string_view wire);

std::string encode_lobby_seed(std::uint32_t seed);
std::optional<std::uint32_t> decode_lobby_seed(std::string_view wire) noexcept;

} // namespace Network

#endif
