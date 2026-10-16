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
enum class RoomOperation { Create, Join, List, Leave, TransferHost, SetPolicy,
    SetRole, StartMatch, SetReady, ForceStart, CancelMatch, SetLimits, Kick, SetMemberRole, SuggestName,
    Prepare, EndMatch };
enum class RoomResult { Created, Joined, Listed, Left, NameTaken, NotFound, Full, InvalidName, AlreadyInRoom, Busy,
    HostTransferred, PolicyChanged, HostOnly, InvalidPlayer, RoundBusy,
    RoleChanged, MatchStarted, ReadyChanged, MatchForced, MatchCanceled, NeedReadyPlayers, Incompatible,
    LimitsChanged, BattleFull, LimitBelowMembers, PlayerKicked, OfflineRestricted, NameSuggested };
enum class PlayerRole { Battle, Spectator, Offline };
enum class RoomMatchPhase { Idle, Preparing, Countdown, Playing, Finished };
inline constexpr int kMultiplayerRoomCapacity = 32;
struct RoomPolicy {
    bool forceRules = true;
    bool operator==(const RoomPolicy& other) const { return forceRules==other.forceRules; }
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
    PlayerRole role = PlayerRole::Battle;
    bool ready = false;
    bool participating = false;
    bool alive = false;
    int killer = -1, target = -1;
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
    std::uint64_t matchEpoch = 0;
    int battleLimit = kMultiplayerRoomCapacity;
    PlayerRole memberRole = PlayerRole::Battle;
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
    bool multiplayer = false;
    RoomMatchPhase matchPhase = RoomMatchPhase::Idle;
    std::uint64_t matchEpoch = 0;
    int battleLimit = kMultiplayerRoomCapacity;
};
bool room_result_has_state(RoomResult result) noexcept;
struct PlayerWirePacket {
    std::uint64_t epoch = 0;
    int slot = -1;
    std::string data;
};
std::string encode_player_packet(const PlayerWirePacket& packet);
std::optional<PlayerWirePacket> decode_player_packet(std::string_view wire);
std::string encode_match_packet(std::uint64_t epoch,std::string_view data);
std::optional<std::pair<std::uint64_t,std::string>> decode_match_packet(std::string_view wire);
// Requests have no remainingMicros. Replies use -1 before the server has
// chosen a start time, or 0..3,000,000 until that monotonic deadline.
struct CountdownClockPacket {
    std::uint64_t probe = 0;
    std::optional<int> remainingMicros;
};
std::string encode_countdown_clock(const CountdownClockPacket& packet);
std::optional<CountdownClockPacket> decode_countdown_clock(std::string_view wire);
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
