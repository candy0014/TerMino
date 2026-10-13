#include "tetr/network/network_services.h"

#include "tetr/runtime/game_context.h"
#include "tetr/config/user_config.h"
#include "tetr/network/frame_packet.h"

#include <stdexcept>
#include <algorithm>
#include "tetr/network/lobby_protocol.h"

namespace Network {

NetworkServices& network_services(Runtime::GameContext& context) {
    if (!context.network_component()) {
        throw std::logic_error("GameContext has no NetworkServices attached");
    }
    return *context.network_component();
}

const NetworkServices& network_services(const Runtime::GameContext& context) {
    if (!context.network_component()) {
        throw std::logic_error("GameContext has no NetworkServices attached");
    }
    return *context.network_component();
}

void disconnect_online(NetworkServices& services) {
    if ((services.socket || services.webSocket) && services.initialized) {
        try { send_online(services, "!leave;" + services.connectionToken); }
        catch (const UdpException&) {}
    }
    services.webSocket.reset();
    services.socket.reset();
    services.initialized = false;
    services.resolvedServerIp.clear();
    services.serverPort = 3000;
    services.connection = ConnectionState::Disconnected;
    services.connectionMessage = "Disconnected";
    services.onlineInbox.clear();
    services.pieceTurns = {}; services.boardSync = {};
    services.roomPolicy={}; services.roomPolicyAvailable=false; services.roomInstance=0; services.roomConfig={}; services.localRoomConfig.clear(); services.lastAppliedConfigEpoch=0;
    services.room = {}; services.roomPlayerIndex = -1; services.roomMembers.clear(); services.confirmedUsername.clear(); services.confirmedUsernameRevision = 0;
    services.publicRooms.clear(); services.pendingRoomRequest.reset();
    services.nextRoomRequest = 0; services.lastRoomStateRevision = 0;
    services.roomMessage = "Create or join a room before starting a game.";
    ++services.roomRevision;
}

void send_online(NetworkServices& services, const std::string& message) {
    if (services.webSocket) services.webSocket->send(message);
    else if (services.socket) services.socket->send(services.resolvedServerIp, services.serverPort, message);
    else throw UdpException("Online transport is not initialized.");
}

void initialize_network_services(Runtime::GameContext& context, const std::string& serverIp) {
    auto& network = network_services(context);
    try {
        const bool useWebSocket = WebSocketSocket::recognizes(serverIp);
        const auto address = useWebSocket ? UdpAddress{} : UdpSocket::resolve_address(serverIp, 3000);
        // Reuse the endpoint for a reconnect to the same server. Its occupied
        // slot can then be replaced without a leave/join UDP ordering race.
        // If DNS changed, leave using the old address and token first.
        if (network.serverIp != serverIp || (useWebSocket &&
            (!network.webSocket || network.connection == ConnectionState::Failed)) ||
            (!useWebSocket && (!network.socket || network.resolvedServerIp != address.ip || network.serverPort != address.port)))
            disconnect_online(network);
        network.room = {}; network.roomPlayerIndex = -1; network.roomMembers.clear(); network.confirmedUsername.clear(); network.confirmedUsernameRevision = 0; network.lastUsernameSend = {}; network.usernameRevision = 1;
        network.publicRooms.clear(); network.pendingRoomRequest.reset(); network.nextRoomRequest = 0; network.lastRoomStateRevision = 0;
        network.roomMessage = "Create or join a room before starting a game.";
        ++network.roomRevision;
        network.onlineInbox.clear();
        network.pieceTurns = {}; network.boardSync = {};
        network.roomPolicy={}; network.roomPolicyAvailable=false; network.roomInstance=0; network.roomConfig={}; network.localRoomConfig.clear(); network.lastAppliedConfigEpoch=0;
        network.serverIp = serverIp;
        if (context.user_settings_component()) network.username = context.user_settings().username;
        if (!valid_room_name(network.username)) {
            network.username = "GUEST-";
            for (int i = 0; i < 8; ++i) network.username += static_cast<char>('A' + context.services.random() % 26);
        }
        network.resolvedServerIp = address.ip;
        network.serverPort = address.port;
        network.connectionToken.clear();
        for (int i = 0; i < 20; ++i)
            network.connectionToken += static_cast<char>(context.services.random() % 26 + 'a');
        if (useWebSocket && !network.webSocket) network.webSocket = std::make_unique<WebSocketSocket>(serverIp);
        if (!useWebSocket && !network.socket) {
            network.socket = std::make_unique<UdpSocket>();
            network.socket->bind(0); // Let the OS allocate a free client port.
            network.socket->set_non_blocking();
        }
        network.initialized = true;
        network.connection = ConnectionState::Connecting;
        network.connectionMessage = "Connecting to " + serverIp + "...";
        network.lastConnectionSend = {};
        network.lastConnectionReply = std::chrono::steady_clock::now();
        poll_connection(network);
    } catch (const UdpException& error) {
        disconnect_online(network);
        network.serverIp = serverIp;
        network.connection = ConnectionState::Failed;
        network.connectionMessage = error.what();
    }
}

void set_online_username(NetworkServices& services, const std::string& username) {
    const auto normalized = normalize_room_name(username);
    if (valid_room_name(normalized) && services.username != normalized) {
        services.username = normalized; ++services.usernameRevision; services.lastUsernameSend = {}; ++services.roomRevision;
    }
}

bool is_room_host(const NetworkServices& services) {
    return std::any_of(services.roomMembers.begin(),services.roomMembers.end(),[&](const auto& member) {
        return member.slot==services.roomPlayerIndex && member.host;
    });
}
void transfer_room_host(NetworkServices& services,int slot) {
    request_room(services,RoomOperation::TransferHost,{},true,false,slot,services.roomPolicy);
}
void set_room_policy(NetworkServices& services,RoomPolicy policy) {
    request_room(services,RoomOperation::SetPolicy,{},true,false,-1,policy);
}
void request_room(NetworkServices& services, RoomOperation operation, const std::string& name, bool isPublic, bool quiet,int targetSlot,RoomPolicy policy) {
    if (services.connection != ConnectionState::Connected) {
        services.roomMessage = "Connect to the server before choosing a room."; ++services.roomRevision; return;
    }
    if (services.pendingRoomRequest) {
        if (services.pendingRoomRequest->operation == RoomOperation::List && operation != RoomOperation::List)
            services.pendingRoomRequest.reset(); // A click takes priority over passive lobby refresh.
        else return;
    }
    const auto normalized = normalize_room_name(name);
    if ((operation == RoomOperation::Create || operation == RoomOperation::Join) &&
        !valid_room_name(normalized, operation == RoomOperation::Create)) {
        services.roomMessage = std::string(room_result_message(RoomResult::InvalidName)); ++services.roomRevision; return;
    }
    services.pendingRoomRequest = RoomRequest{services.connectionToken, ++services.nextRoomRequest, operation, normalized, isPublic,targetSlot,policy};
    services.quietRoomRequest = quiet;
    if (!quiet) services.roomMessage = "Waiting for the server...";
    services.roomRequestStarted = std::chrono::steady_clock::now(); services.lastRoomSend = {};
    ++services.roomRevision;
    poll_connection(services);
}

void poll_connection(NetworkServices& services, std::chrono::milliseconds wait) {
    if ((!services.socket && !services.webSocket) || (services.connection != ConnectionState::Connecting &&
        services.connection != ConnectionState::Connected)) return;
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    if (now - services.lastConnectionReply > std::chrono::seconds(5)) {
        services.connection = ConnectionState::Failed;
        services.connectionMessage = "Server unavailable. Reconnect or edit Server IP.";
        services.roomMessage = services.connectionMessage; services.pendingRoomRequest.reset(); ++services.roomRevision;
        return;
    }
    try {
        if (services.connection == ConnectionState::Connected && services.usernameRevision != services.confirmedUsernameRevision &&
            now - services.lastUsernameSend >= std::chrono::milliseconds(250)) {
            send_online(services,encode_username_update({services.connectionToken,services.username,services.usernameRevision})); services.lastUsernameSend = now;
        }
        if (services.pendingRoomRequest) {
            if (now - services.roomRequestStarted > std::chrono::seconds(5)) {
                services.pendingRoomRequest.reset();
                services.roomMessage = "Room request timed out. Refresh or try again."; ++services.roomRevision;
            } else if (now - services.lastRoomSend >= std::chrono::milliseconds(250)) {
                send_online(services, encode_room_request(*services.pendingRoomRequest)); services.lastRoomSend = now;
            }
        }
        if (now - services.lastConnectionSend >= std::chrono::milliseconds(250)) {
            send_online(services, (services.connection == ConnectionState::Connecting ? "!join-v2;" : "!ping;") + services.connectionToken);
            services.lastConnectionSend = now;
        }
        const auto receiveOnline = [&](std::chrono::milliseconds timeout) -> std::optional<std::string> {
            if (services.webSocket) return services.webSocket->receive_for(timeout);
            auto datagram = services.socket->receive_for(timeout);
            while (datagram) {
                if (datagram->address.ip == services.resolvedServerIp && datagram->address.port == services.serverPort)
                    return std::move(datagram->data);
                datagram = services.socket->try_receive();
            }
            return std::nullopt;
        };
        auto message = receiveOnline(wait);
        while (message) {
            const auto& wire = *message;
            services.lastConnectionReply = Clock::now();
            if (wire == "!joined;" + services.connectionToken || wire == "!pong;" + services.connectionToken) {
                services.connection = ConnectionState::Connected;
                services.connectionMessage = "Connected to " + services.serverIp;
            } else if (wire == "!full;" + services.connectionToken) {
                services.connection = ConnectionState::Full;
                services.connectionMessage = "Server connection limit reached.";
            } else if (wire == "!expired;" + services.connectionToken) {
                services.connection = ConnectionState::Failed;
                services.connectionMessage = "Connection expired. Reconnect to the server.";
            } else if (const auto username = decode_username_update(wire,true)) {
                if (username->token == services.connectionToken && username->revision == services.usernameRevision && username->username == services.username) {
                    services.confirmedUsername = username->username; services.confirmedUsernameRevision = username->revision;
                }
            } else if (auto reply = decode_room_response(wire)) {
                if (reply->token == services.connectionToken &&
                    (reply->id == 0 || (services.pendingRoomRequest && reply->id == services.pendingRoomRequest->id))) {
                    if (reply->id != 0) {
                        services.pendingRoomRequest.reset();
                        if (!services.quietRoomRequest) services.roomMessage = std::string(room_result_message(reply->result));
                        if (reply->result == RoomResult::Listed) services.publicRooms = reply->rooms;
                        ++services.roomRevision;
                    }
                    if (reply->stateRevision >= services.lastRoomStateRevision &&
                        (reply->result == RoomResult::Created || reply->result == RoomResult::Joined || reply->result == RoomResult::Left ||
                         reply->result==RoomResult::HostTransferred || reply->result==RoomResult::PolicyChanged)) {
                        services.lastRoomStateRevision = reply->stateRevision;
                        if (services.room.name != reply->room.name || services.roomInstance!=reply->roomInstance) {
                            services.onlineInbox.clear(); services.pieceTurns = {}; services.boardSync = {};
                            services.roomConfig={}; services.lastAppliedConfigEpoch=0;
                        }
                        const bool sameMembers = services.roomMembers.size() == reply->members.size() &&
                            std::equal(services.roomMembers.begin(),services.roomMembers.end(),reply->members.begin(),[](const auto& a,const auto& b) {
                                return a.slot == b.slot && a.username == b.username && a.host == b.host;
                            });
                        if (!sameMembers || services.room.name != reply->room.name || services.room.players != reply->room.players ||
                            services.roomPlayerIndex != reply->playerIndex || services.roomPolicy!=reply->policy ||
                            services.roomPolicyAvailable!=reply->hasPolicy || services.roomInstance!=reply->roomInstance) ++services.roomRevision;
                        services.room = reply->room; services.roomPlayerIndex = reply->playerIndex; services.roomMembers = reply->members;
                        services.roomPolicy=reply->policy; services.roomPolicyAvailable=reply->hasPolicy;
                        services.roomInstance=reply->roomInstance;
                    }
                }
            } else if (wire == "!round-reset") {
                services.onlineInbox.clear();
                services.roomConfig={};
                services.onlineInbox.push_back("end2");
            } else if (wire.rfind("!config-error;",0)==0) {
                services.roomMessage=wire.substr(14); ++services.roomRevision;
                services.roomConfig={}; services.onlineInbox.clear(); services.onlineInbox.push_back("end2");
            } else if (handle_room_config_message(services,wire)) {
            } else if (services.connection == ConnectionState::Connected && !services.room.name.empty() &&
                       (wire.rfind("!", 0) != 0 || is_gameplay_extension(wire))) {
                if (wire==encode_server_lobby_signal(ServerLobbySignal::ConfigExchangeReady) && services.roomConfig.applied)
                    services.roomConfig.finished=true;
                if (!decode_server_lobby_signal(wire) ||
                    std::find(services.onlineInbox.begin(), services.onlineInbox.end(), wire) == services.onlineInbox.end())
                    services.onlineInbox.push_back(wire);
            }
            message = receiveOnline(std::chrono::milliseconds(0));
        }
        poll_room_config_transfer(services);
    } catch (const UdpException& error) {
        services.connection = ConnectionState::Failed;
        services.connectionMessage = error.what();
        services.roomMessage = services.connectionMessage; services.pendingRoomRequest.reset(); ++services.roomRevision;
    }
}

void connect_local_duplex(NetworkServices& first, NetworkServices& second) {
    auto transport = std::make_shared<LocalDuplexTransport>();
    first.localTransport = transport;
    first.localEndpoint = 0;
    second.localTransport = std::move(transport);
    second.localEndpoint = 1;
}

void disconnect_local_duplex(NetworkServices& services) {
    services.localTransport.reset();
    services.localEndpoint = -1;
}

bool uses_local_duplex(const NetworkServices& services) noexcept {
    return services.localTransport != nullptr &&
        (services.localEndpoint == 0 || services.localEndpoint == 1);
}

} // namespace Network
