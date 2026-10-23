#ifndef TETR_NETWORK_RELAY_SERVER_H
#define TETR_NETWORK_RELAY_SERVER_H

#include "tetr/network/lobby_protocol.h"
#include "tetr/network/round_statistics.h"
#include "tetr/network/udp_socket.h"
#include "tetr/network/room_config_protocol.h"
#include "tetr/network/frame_packet.h"
#include "tetr/network/board_view.h"
#include "tetr/replay/multiplayer_codec.h"
#include <memory>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <map>
#include <set>

namespace Network {
struct RoomReplayArchiveState;
class RelayServer {
public:
    explicit RelayServer(std::uint32_t randomSeed,const std::string& roomCodeHistory = {});
    void bind(UdpPort port);
    // Admission is handled continuously, including during an active round.
    void poll(std::chrono::milliseconds timeout = std::chrono::milliseconds(10));
    [[noreturn]] void run();
private:
    using Clock = std::chrono::steady_clock;
    enum class Phase { Waiting, Preparing, HostConfig, HostConfigAck, Setup, SetupAck, Countdown, Playing, RoundAck };
    struct Client {
        UdpAddress address;
        std::string token;
        Clock::time_point lastSeen;
        std::string room;
        std::uint64_t lastRequest = 0;
        std::string lastResponse;
        // Resend removal state on heartbeat until a new membership is created.
        std::string evictionResponse;
        std::string reservedRoomName;
        std::uint64_t stateRevision = 0;
        std::string username;
        std::uint64_t usernameRevision = 0;
        bool roomConfigCapable = false;
        bool multiplayerCapable = false;
        bool relayBatchCapable = false;
        bool roomReplayCapable = false;
        std::string replayViewerIdentity;
    };
    struct Room {
        struct AttackDelivery { int source=-1, sourceSequence=0, sequence=0, amount=0; };
        std::uint64_t instance=0;
        bool isPublic = true;
        std::string host;
        RoomPolicy policy;
        bool multiplayer = false;
        bool manualPreparation = false, startRequested = false;
        int capacity = 2;
        int memberLimit = 2, battleLimit = 2;
        std::uint64_t matchEpoch = 0;
        std::map<std::uint64_t,std::shared_ptr<RoomReplayArchiveState>> replays;
        int winner = -1;
        std::array<std::string, kMultiplayerRoomCapacity> players{};
        Phase phase = Phase::Waiting;
        std::array<bool, kMultiplayerRoomCapacity> ready{}, active{}, entered{}, configApplied{}, participating{}, alive{};
        std::array<bool, kMultiplayerRoomCapacity> countdownClockCapable{};
        bool scheduledCountdown = false;
        std::array<PlayerRole,kMultiplayerRoomCapacity> roles{};
        std::array<std::string, kMultiplayerRoomCapacity> setup{}, snapshots{};
        std::array<std::optional<BoardViews>,kMultiplayerRoomCapacity> boardViews{};
        std::array<std::optional<MiniBoardFrame>,kMultiplayerRoomCapacity> miniBoards{};
        // Reuse common terminal-sized projections across viewers. At most
        // four variants per actor; cells are invalidated by a new source frame.
        std::array<std::map<std::pair<int,int>,MiniBoardFrame>,kMultiplayerRoomCapacity> miniProjections{};
        std::array<Clock::time_point,kMultiplayerRoomCapacity> miniBoardReceived{};
        std::array<std::string,kMultiplayerRoomCapacity> boardBudgets{};
        std::array<std::array<Clock::time_point,kMultiplayerRoomCapacity>,kMultiplayerRoomCapacity> lastMiniForward{};
        std::array<std::array<std::uint64_t,kMultiplayerRoomCapacity>,kMultiplayerRoomCapacity> miniForwardSequence{};
        std::array<Clock::time_point,kMultiplayerRoomCapacity> lastMiniBatch{};
        std::array<std::size_t,kMultiplayerRoomCapacity> miniBatchCursor{};
        std::array<std::string,kMultiplayerRoomCapacity> results{};
        std::array<ResultAssembler,kMultiplayerRoomCapacity> resultUploads{};
        struct ResultHistory {
            std::uint64_t epoch=0;
            std::array<std::string,kMultiplayerRoomCapacity> actors{},results{};
            std::array<ResultAssembler,kMultiplayerRoomCapacity> uploads{};
        };
        std::optional<ResultHistory> previousResults;
        std::array<int,kMultiplayerRoomCapacity> target{}, lastAttacker{}, killer{}, sentAttackId{}, receivedAttackId{};
        std::array<int,kMultiplayerRoomCapacity> attackAckCursor{};
        std::array<std::set<int>,kMultiplayerRoomCapacity> seenAttacks, deliveredAttacks;
        std::array<std::set<int>,kMultiplayerRoomCapacity> attackSources;
        std::array<std::map<int,int>,kMultiplayerRoomCapacity> attackTargets;
        std::array<std::deque<AttackDelivery>,kMultiplayerRoomCapacity> pendingAttacks;
        Clock::time_point lastTargetSelection{};
        std::string seed;
        Clock::time_point lastPublish{}, lastSetupPublish{}, lastConfigPublish{}, phaseStarted{};
        std::uint64_t configEpoch=0;
        ConfigAssembler config;
        std::array<std::vector<bool>,kMultiplayerRoomCapacity> configReceived;
        std::array<std::size_t,kMultiplayerRoomCapacity> configCursor{};
        RoomConfigState preview;
        std::uint64_t previewBroadcastEpoch=0;
        std::array<std::vector<bool>,kMultiplayerRoomCapacity> previewReceived;
        std::array<std::size_t,kMultiplayerRoomCapacity> previewCursor{};
        Clock::time_point lastPreviewPublish{};
    };
    void handle(const Datagram& datagram);
    void handle_room(Client& client, const RoomRequest& request);
    bool handle_config(Client& client,const std::string& key,std::string_view wire);
    void publish_preview(Room& room);
    void leave_room(Client& client);
    void reset_round(Room& room, int except = -1);
    void publish_phase(Room& room);
    void begin_multiplayer_match(Room& room);
    void restart_multiplayer_preparation(Room& room);
    RoomResult handle_multiplayer_request(Room& room,Client& client,const RoomRequest& request);
    bool start_ready_players(Room& room,bool force);
    void advance_scheduled_countdown(Room& room,const std::string& name);
    void publish_multiplayer_phase(Room& room);
    void handle_multiplayer_gameplay(Room& room,Client& client,int slot,std::string_view wire);
    void eliminate_player(Room& room,int slot);
    void start_room_replay(Room& room,std::optional<Clock::time_point> started={});
    void finish_room_replay(Room& room,Replay::RoundEndReason reason=Replay::RoundEndReason::Normal);
    void record_room_replay_elimination(Room& room,int slot);
    void record_room_replay_targets(Room& room);
    bool handle_room_replay(Client& client,std::string_view wire);
    void update_board_budgets(Room& room,int resendSlot=-1);
    void flush_mini_boards(Room& room,Clock::time_point now);
    void acknowledge_delivery(Room& room,int source,int sequence);
    void select_targets(Room& room,bool force=false);
    RoomMatchPhase match_phase(const Room& room) const;
    std::vector<RoomMember> room_members(const Room& room) const;
    void publish_room(const std::string& name, Room& room,int onlySlot=-1);
    void send_wire(const Room& room, int index, const std::string& wire);
    void send_mini_batch(const Room& room,int index,const std::vector<std::string>& events);
    std::string sender_key(const UdpAddress& address) const;
    std::string reserve_room_name(Client& client);
    void remember_room_code(const std::string& name);
    std::mt19937 random_;
    UdpSocket socket_;
    std::map<std::string, Client> clients_;
    std::map<std::string, Room> rooms_;
    std::set<std::string> usedRoomCodes_;
    std::string roomCodeHistory_;
    std::uint64_t nextRoomInstance_=0, nextConfigEpoch_=0, nextMatchEpoch_=0;
};
} // namespace Network
#endif
