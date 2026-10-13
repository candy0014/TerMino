#ifndef TETR_NETWORK_NETWORK_SERVICES_H
#define TETR_NETWORK_NETWORK_SERVICES_H

#include "tetr/network/udp_socket.h"
#include "tetr/network/websocket_socket.h"
#include "tetr/network/piece_turn.h"
#include "tetr/network/lobby_protocol.h"
#include "tetr/network/hybrid_sync.h"
#include "tetr/network/room_config_protocol.h"

#include <array>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <chrono>
#include <stdexcept>

namespace Runtime { class GameContext; }

namespace Network {

enum class ConnectionState { Disconnected, Connecting, Connected, Full, Failed };
class ConnectionError : public std::runtime_error { using std::runtime_error::runtime_error; };
class LobbyInterrupted : public std::runtime_error { using std::runtime_error::runtime_error; };

struct LocalDuplexTransport {
    std::mutex mutex;
    std::condition_variable available;
    std::array<std::deque<std::string>, 2> inbox;
};

struct NetworkServices {
    // Created only when online multiplayer is selected. Local-versus uses the
    // in-memory transport and therefore needs neither a socket nor a server.
    std::unique_ptr<UdpSocket> socket;
    std::unique_ptr<WebSocketSocket> webSocket;
    // Keep the configured hostname for UI/settings comparisons, and pin the
    // resolved address for both outgoing traffic and incoming peer checks.
    std::string serverIp;
    std::string resolvedServerIp;
    UdpPort serverPort = 3000;
    bool initialized = false;
    ConnectionState connection = ConnectionState::Disconnected;
    std::string connectionMessage = "Disconnected";
    std::string connectionToken;
    std::deque<std::string> onlineInbox;
    std::chrono::steady_clock::time_point lastConnectionSend{}, lastConnectionReply{};
    std::string username, confirmedUsername;
    std::uint64_t usernameRevision = 1, confirmedUsernameRevision = 0;
    bool quietRoomRequest = false;
    std::chrono::steady_clock::time_point lastUsernameSend{};
    RoomInfo room;
    std::vector<RoomMember> roomMembers;
    RoomPolicy roomPolicy;
    bool roomPolicyAvailable = false;
    std::uint64_t roomInstance = 0;
    RoomConfigState roomConfig;
    std::string localRoomConfig;
    std::uint64_t lastAppliedConfigEpoch = 0;
    int roomPlayerIndex = -1;
    std::vector<RoomInfo> publicRooms;
    std::string roomMessage = "Create or join a room before starting a game.";
    std::optional<RoomRequest> pendingRoomRequest;
    std::uint64_t nextRoomRequest = 0, roomRevision = 0, lastRoomStateRevision = 0;
    std::chrono::steady_clock::time_point lastRoomSend{}, roomRequestStarted{};
    std::shared_ptr<LocalDuplexTransport> localTransport;
    int localEndpoint = -1;
    PieceTurnState pieceTurns;
    HybridBoardSync boardSync;
};

NetworkServices& network_services(Runtime::GameContext& context);
const NetworkServices& network_services(const Runtime::GameContext& context);
void initialize_network_services(Runtime::GameContext& context, const std::string& serverIp);
void disconnect_online(NetworkServices& services);
void send_online(NetworkServices& services, const std::string& message);
void poll_connection(NetworkServices& services, std::chrono::milliseconds wait = std::chrono::milliseconds(0));
void set_online_username(NetworkServices& services, const std::string& username);
void request_room(NetworkServices& services, RoomOperation operation, const std::string& name = {}, bool isPublic = true, bool quiet = false,
    int targetSlot = -1, RoomPolicy policy = {});
bool is_room_host(const NetworkServices& services);
void transfer_room_host(NetworkServices& services,int slot);
void set_room_policy(NetworkServices& services,RoomPolicy policy);
bool handle_room_config_message(NetworkServices& services,std::string_view wire);
void poll_room_config_transfer(NetworkServices& services);
void accept_room_config(NetworkServices& services);
void connect_local_duplex(NetworkServices& first, NetworkServices& second);
void disconnect_local_duplex(NetworkServices& services);
bool uses_local_duplex(const NetworkServices& services) noexcept;

} // namespace Network

#endif
