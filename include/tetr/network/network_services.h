#ifndef TETR_NETWORK_NETWORK_SERVICES_H
#define TETR_NETWORK_NETWORK_SERVICES_H

#include "tetr/network/udp_socket.h"
#include "tetr/network/websocket_socket.h"
#include "tetr/network/piece_turn.h"
#include "tetr/network/lobby_protocol.h"
#include "tetr/network/hybrid_sync.h"
#include "tetr/network/board_view.h"
#include "tetr/network/room_config_protocol.h"

#include <array>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <chrono>
#include <stdexcept>
#include <map>

namespace Runtime { class GameContext; }

namespace Network {
inline constexpr auto kConnectedTimeout=std::chrono::seconds(15);

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
    int protocolVersion = 4;
    std::deque<std::string> onlineInbox;
    std::chrono::steady_clock::time_point lastConnectionSend{}, lastConnectionReply{};
    std::string username, confirmedUsername;
    std::uint64_t usernameRevision = 1, confirmedUsernameRevision = 0;
    bool quietRoomRequest = false;
    std::chrono::steady_clock::time_point lastUsernameSend{};
    RoomInfo room;
    std::string suggestedRoomName;
    std::vector<RoomMember> roomMembers;
    RoomPolicy roomPolicy;
    bool roomPolicyAvailable = false;
    std::uint64_t roomInstance = 0;
    RoomConfigState roomConfig;
    std::string localRoomConfig;
    RoomConfigState roomPreview;
    std::string localRoomPreview;
    std::uint64_t nextPreviewEpoch=0, previewRevision=0;
    std::chrono::steady_clock::time_point lastPreviewBuild{};
    std::uint64_t lastAppliedConfigEpoch = 0;
    int roomPlayerIndex = -1;
    bool multiplayerRoom = false;
    int battleLimit = kMultiplayerRoomCapacity;
    RoomMatchPhase matchPhase = RoomMatchPhase::Idle;
    std::uint64_t matchEpoch = 0, matchNoticeEpoch = 0;
    std::chrono::steady_clock::time_point matchNoticeUntil{};
    std::chrono::steady_clock::time_point lastNoticePaint{};
    bool matchCanceled = false;
    bool countdownClockAvailable = false;
    bool countdownClockRequested = false;
    bool countdownLegacy = false;
    bool countdownStarted = false;
    std::optional<std::chrono::steady_clock::time_point> countdownDeadline;
    std::chrono::steady_clock::duration countdownBestRtt = std::chrono::seconds(15);
    std::uint64_t countdownProbe = 0;
    std::map<std::uint64_t,std::chrono::steady_clock::time_point> countdownProbes;
    std::chrono::steady_clock::time_point lastCountdownProbe{};
    bool startMatchQueued = false, startFromMenu = false;
    std::optional<bool> roomOnlineMode;
    std::optional<int> roomGameMode;
    std::uint64_t roleRoomInstance = 0;
    std::optional<RoomOperation> queuedMatchControl;
    int winnerSlot = -1, targetSlot = -1, deliveredAttack = 0, receivedAttack = 0;
    std::map<std::uint64_t,std::map<int,std::string>> roundResultWires,roundResultNames;
    std::map<std::uint64_t,std::vector<int>> roundParticipants;
    std::map<std::uint64_t,std::map<int,std::shared_ptr<class ResultAssembler>>> resultIncoming;
    std::uint64_t resultRoomInstance=0;
    std::optional<std::uint64_t> pendingResultEpoch;
    std::string pendingRoundResult;
    std::size_t pendingResultCursor=0;
    std::chrono::steady_clock::time_point lastResultSend{};
    std::map<int,LobbyPlayerSetup> playerSetups;
    std::deque<PlayerWirePacket> playerInbox;
    std::map<int,RemoteBoardTimeline> peerTimelines;
    std::map<int,BoardStateSnapshot> peerSnapshots;
    std::map<int,MiniBoardFrame> peerMiniBoards;
    std::optional<BoardBudget> boardBudget;
    BoardViews boardViews;
    bool boardViewsEnabled=true; // Capability fallback; also used by matched transport benchmarks.
    bool boardViewsV2Confirmed=false;
    int miniUpdateBudget=240;
    std::array<std::chrono::steady_clock::duration,3> visualRtts{};
    std::size_t visualRttSamples=0;
    std::chrono::steady_clock::time_point lastVisualRateChange{},visualGoodSince{};
    std::string lastBoardViews;
    Runtime::TimePoint lastBoardViewSend{},lastMiniSend{},lastMiniSample{};
    std::optional<MiniBoardFrame> lastMiniFrame;
    bool forceFullCheckpoint=false;
    std::uint64_t miniSequence=0,miniFramesSent=0,miniBytesSent=0;
    std::map<int,std::uint64_t> peerTurns;
    std::map<int,FramePacket> incomingAttacks;
    std::map<int,std::string> outgoingAttacks;
    std::map<int,int> actualAttackTargets;
    std::chrono::steady_clock::time_point lastAttackSend{};
    std::string localPlayerSetup;
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
void initialize_network_services(Runtime::GameContext& context, const std::string& serverIp, int protocolVersion = 4);
void disconnect_online(NetworkServices& services);
void send_online(NetworkServices& services, const std::string& message);
void poll_connection(NetworkServices& services, std::chrono::milliseconds wait = std::chrono::milliseconds(0),
                     std::chrono::microseconds processingBudget = std::chrono::microseconds(2000));
void set_online_username(NetworkServices& services, const std::string& username);
void request_room(NetworkServices& services, RoomOperation operation, const std::string& name = {}, bool isPublic = true, bool quiet = false,
    int targetSlot = -1, RoomPolicy policy = {}, int battleLimit = kMultiplayerRoomCapacity,
    PlayerRole memberRole = PlayerRole::Battle);
void reset_multiplayer_state(NetworkServices& services);
void apply_multiplayer_state(NetworkServices& services,const RoomResponse& reply);
bool handle_multiplayer_message(NetworkServices& services,std::string_view wire);
void poll_multiplayer_transfer(NetworkServices& services);
void synchronize_room_player_mode(NetworkServices& services,bool onlineMode,std::optional<int> gameMode=std::nullopt);
std::optional<FramePacket> try_receive_multiplayer_frame(Runtime::GameContext& context,bool pollConnection = true,
    std::chrono::microseconds processingBudget = std::chrono::microseconds(1000));
bool is_room_host(const NetworkServices& services);
void transfer_room_host(NetworkServices& services,int slot);
void set_room_policy(NetworkServices& services,RoomPolicy policy);
bool handle_room_config_message(NetworkServices& services,std::string_view wire);
void poll_room_config_transfer(NetworkServices& services);
void accept_room_config(NetworkServices& services);
void set_room_config_preview(NetworkServices& services,const std::string& payload);
void connect_local_duplex(NetworkServices& first, NetworkServices& second);
void disconnect_local_duplex(NetworkServices& services);
bool uses_local_duplex(const NetworkServices& services) noexcept;

} // namespace Network

#endif
