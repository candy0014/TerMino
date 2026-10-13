#ifndef TETR_NETWORK_RELAY_SERVER_H
#define TETR_NETWORK_RELAY_SERVER_H

#include "tetr/network/lobby_protocol.h"
#include "tetr/network/udp_socket.h"
#include "tetr/network/room_config_protocol.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <map>

namespace Network {
class RelayServer {
public:
    explicit RelayServer(std::uint32_t randomSeed);
    void bind(UdpPort port);
    // Admission is handled continuously, including during an active round.
    void poll(std::chrono::milliseconds timeout = std::chrono::milliseconds(10));
    [[noreturn]] void run();
private:
    using Clock = std::chrono::steady_clock;
    enum class Phase { Waiting, HostConfig, HostConfigAck, Setup, SetupAck, Countdown, Playing, RoundAck };
    struct Client {
        UdpAddress address;
        std::string token;
        Clock::time_point lastSeen;
        std::string room;
        std::uint64_t lastRequest = 0;
        std::string lastResponse;
        std::uint64_t stateRevision = 0;
        std::string username;
        std::uint64_t usernameRevision = 0;
        bool roomConfigCapable = false;
    };
    struct Room {
        std::uint64_t instance=0;
        bool isPublic = true;
        std::string host;
        RoomPolicy policy;
        std::array<std::string, 2> players{};
        Phase phase = Phase::Waiting;
        std::array<bool, 2> ready{}, active{};
        std::array<std::string, 2> setup{};
        std::string seed;
        Clock::time_point lastPublish{}, phaseStarted{};
        std::uint64_t configEpoch=0;
        ConfigAssembler config;
        std::array<std::vector<bool>,2> configReceived;
        std::array<std::size_t,2> configCursor{};
    };
    void handle(const Datagram& datagram);
    void handle_room(Client& client, const RoomRequest& request);
    bool handle_config(Client& client,const std::string& key,std::string_view wire);
    void leave_room(Client& client);
    void reset_round(Room& room, int except = -1);
    void publish_phase(Room& room);
    std::vector<RoomMember> room_members(const Room& room) const;
    void publish_room(const std::string& name, Room& room);
    void send_wire(const Room& room, int index, const std::string& wire);
    std::string sender_key(const UdpAddress& address) const;
    std::mt19937 random_;
    UdpSocket socket_;
    std::map<std::string, Client> clients_;
    std::map<std::string, Room> rooms_;
    std::uint64_t nextRoomInstance_=0, nextConfigEpoch_=0;
};
} // namespace Network
#endif
