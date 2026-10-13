#include "tetr/network/relay_server.h"
#include "tetr/network/frame_packet.h"
#include <algorithm>

namespace Network {
RelayServer::RelayServer(std::uint32_t randomSeed) : random_(randomSeed),
    nextRoomInstance_(static_cast<std::uint64_t>(randomSeed)<<32),nextConfigEpoch_(static_cast<std::uint64_t>(randomSeed)<<32) {}
void RelayServer::bind(UdpPort port) { socket_.bind(port); socket_.set_non_blocking(); }
std::string RelayServer::sender_key(const UdpAddress& address) const {
    return address.ip + ":" + std::to_string(address.port);
}
void RelayServer::send_wire(const Room& room, int index, const std::string& wire) {
    const auto client = clients_.find(room.players[index]);
    if (client != clients_.end()) socket_.send(client->second.address, wire);
}
void RelayServer::reset_round(Room& room, int except) {
    if (room.phase != Phase::Waiting)
        for (int i = 0; i < 2; ++i) if (i != except) send_wire(room, i, "!round-reset");
    room.phase = Phase::Waiting;
    room.ready = {}; room.active = {}; room.setup = {};
    room.config={}; room.configReceived={}; room.configCursor={};
    room.lastPublish = {}; room.phaseStarted = Clock::now();
}
void RelayServer::publish_phase(Room& room) {
    if (room.players[0].empty() || room.players[1].empty()) return;
    for (int i = 0; i < 2; ++i) {
        switch (room.phase) {
            case Phase::Waiting: send_wire(room, i, std::string(encode_server_lobby_signal(ServerLobbySignal::PairReady))); break;
            case Phase::HostConfig:
            case Phase::HostConfigAck: {
                const auto client=clients_.find(room.players[i]);
                if (client==clients_.end()) break;
                ConfigMessage begin; begin.token=client->second.token; begin.epoch=room.configEpoch;
                begin.roomInstance=room.instance;
                begin.hostSlot=static_cast<int>(std::find(room.players.begin(),room.players.end(),room.host)-room.players.begin());
                begin.policy=room.policy; send_wire(room,i,encode_config_message(begin));
                if (room.phase==Phase::HostConfigAck && room.config.payload && !room.ready[i]) {
                    const auto count=room.config.parts.size(); std::size_t scanned=0,sent=0;
                    while (scanned++<count && sent<32) {
                        const auto index=room.configCursor[i]++%count;
                        if (!room.configReceived[i][index]) {
                            send_wire(room,i,encode_config_message(room_config_part(*room.config.payload,client->second.token,
                                room.configEpoch,room.config.digest,index))); ++sent;
                        }
                    }
                }
                break;
            }
            case Phase::Setup:
                send_wire(room, i, std::string(encode_server_lobby_signal(ServerLobbySignal::ConfigExchangeReady)));
                send_wire(room, i, room.seed); break;
            case Phase::SetupAck: send_wire(room, i, room.setup[1-i]); break;
            case Phase::Countdown: send_wire(room, i, std::string(encode_server_lobby_signal(ServerLobbySignal::CountdownReady))); break;
            case Phase::Playing:
                if (!room.active[i]) send_wire(room, i, std::string(encode_server_lobby_signal(ServerLobbySignal::RoundReady)));
                break;
            case Phase::RoundAck: break;
        }
    }
    room.lastPublish = Clock::now();
}
std::vector<RoomMember> RelayServer::room_members(const Room& room) const {
    std::vector<RoomMember> members;
    for (std::size_t slot = 0; slot < room.players.size(); ++slot) {
        const auto client = clients_.find(room.players[slot]);
        if (client != clients_.end()) members.push_back({static_cast<int>(slot),client->second.username,room.host == client->first});
    }
    return members;
}
void RelayServer::publish_room(const std::string& name, Room& room) {
    const int count = !room.players[0].empty() + !room.players[1].empty();
    for (int i = 0; i < 2; ++i) {
        const auto client = clients_.find(room.players[i]);
        if (client != clients_.end()) {
            RoomResponse state{client->second.token,0,RoomResult::Joined,{name,room.isPublic,count},i,{},++client->second.stateRevision,room_members(room)};
            state.policy=room.policy; state.hasPolicy=client->second.roomConfigCapable;
            state.roomInstance=room.instance;
            send_wire(room,i,encode_room_response(state));
        }
    }
}
void RelayServer::leave_room(Client& client) {
    const auto found = rooms_.find(client.room);
    if (found == rooms_.end()) { client.room.clear(); return; }
    auto& room = found->second;
    for (int i = 0; i < 2; ++i) if (room.players[i] == sender_key(client.address)) {
        reset_round(room, i);
        // Also interrupt a player who was waiting before the round started.
        send_wire(room, 1-i, "!round-reset");
        room.players[i].clear();
        if (room.host == sender_key(client.address)) {
            std::vector<std::string> candidates;
            for (const auto& player : room.players) if (!player.empty()) candidates.push_back(player);
            room.host = candidates.empty() ? std::string{} : candidates[std::uniform_int_distribution<std::size_t>(0,candidates.size()-1)(random_)];
        }
    }
    client.room.clear();
    if (room.players[0].empty() && room.players[1].empty()) rooms_.erase(found);
    else publish_room(found->first, room);
}
void RelayServer::handle_room(Client& client, const RoomRequest& request) {
    if (request.token != client.token) return;
    // UDP retries must replay the result, including a generated room name.
    if (request.id <= client.lastRequest) {
        if (request.id == client.lastRequest) socket_.send(client.address, client.lastResponse);
        return;
    }
    RoomResponse reply{client.token, request.id, RoomResult::Listed, {}, -1, {}};
    auto name = normalize_room_name(request.name);
    if (request.operation == RoomOperation::List) {
        for (const auto& [key, room] : rooms_) if (room.isPublic)
            reply.rooms.push_back({key, true, static_cast<int>(!room.players[0].empty() + !room.players[1].empty())});
    } else if (request.operation == RoomOperation::Leave) {
        leave_room(client); reply.result = RoomResult::Left;
    } else if (request.operation==RoomOperation::TransferHost || request.operation==RoomOperation::SetPolicy) {
        const auto found=rooms_.find(client.room);
        if (found==rooms_.end()) reply.result=RoomResult::NotFound;
        else {
            auto& room=found->second;
            if (room.host!=sender_key(client.address)) reply.result=RoomResult::HostOnly;
            else if (room.phase!=Phase::Waiting) reply.result=RoomResult::RoundBusy;
            else if (request.operation==RoomOperation::SetPolicy) {
                room.policy=request.policy; room.ready={}; reply.result=RoomResult::PolicyChanged;
            } else if (request.targetSlot<0 || request.targetSlot>=static_cast<int>(room.players.size()) || room.players[request.targetSlot].empty())
                reply.result=RoomResult::InvalidPlayer;
            else {room.host=room.players[request.targetSlot]; room.ready={}; reply.result=RoomResult::HostTransferred;}
        }
    } else if (!client.room.empty()) {
        reply.result = RoomResult::AlreadyInRoom;
    } else if (!valid_room_name(name, request.operation == RoomOperation::Create)) {
        reply.result = RoomResult::InvalidName;
    } else {
        auto found = rooms_.find(name);
        if (request.operation == RoomOperation::Create) {
            if (name.empty()) {
                do {
                    name.clear();
                    for (int i = 0; i < 4; ++i) name += static_cast<char>('A' + random_() % 26);
                } while (rooms_.count(name));
                found = rooms_.end();
            }
            if (found != rooms_.end()) reply.result = RoomResult::NameTaken;
            else {
                found = rooms_.try_emplace(name).first;
                found->second.instance=++nextRoomInstance_;
                found->second.isPublic = request.isPublic;
                reply.result = RoomResult::Created;
            }
        } else reply.result = found == rooms_.end() ? RoomResult::NotFound : RoomResult::Joined;
        if (reply.result == RoomResult::Created || reply.result == RoomResult::Joined) {
            auto& room = found->second;
            const int slot = room.players[0].empty() ? 0 : room.players[1].empty() ? 1 : -1;
            if (slot < 0) reply.result = RoomResult::Full;
            else {
                room.players[slot] = sender_key(client.address); client.room = name;
                if (room.host.empty()) room.host = room.players[slot];
                reply.members = room_members(room);
                reply.room = {name, room.isPublic, static_cast<int>(!room.players[0].empty() + !room.players[1].empty())};
                reply.playerIndex = slot;
            }
        }
    }
    if (reply.result==RoomResult::HostTransferred || reply.result==RoomResult::PolicyChanged) {
        const auto& room=rooms_.at(client.room);
        reply.room={client.room,room.isPublic,static_cast<int>(room_members(room).size())};
        reply.members=room_members(room);
        reply.playerIndex=static_cast<int>(std::find(room.players.begin(),room.players.end(),sender_key(client.address))-room.players.begin());
    }
    reply.hasPolicy=client.roomConfigCapable;
    if (!client.room.empty()) {reply.policy=rooms_.at(client.room).policy; reply.roomInstance=rooms_.at(client.room).instance;}
    reply.stateRevision = ++client.stateRevision;
    client.lastRequest = request.id;
    client.lastResponse = encode_room_response(reply);
    socket_.send(client.address, client.lastResponse);
    if (!client.room.empty()) {
        auto& room = rooms_.at(client.room);
        publish_room(client.room, room); publish_phase(room);
    }
}
bool RelayServer::handle_config(Client& client,const std::string& key,std::string_view wire) {
    if (wire.rfind("!config-",0)!=0) return false;
    const auto message=decode_config_message(wire);
    if (!message || message->token!=client.token || client.room.empty()) return true;
    auto& room=rooms_.at(client.room);
    const auto& m=*message;
    if (m.epoch!=room.configEpoch) return true;
    const auto sender=static_cast<int>(std::find(room.players.begin(),room.players.end(),key)-room.players.begin());
    if (sender>=static_cast<int>(room.players.size())) return true;
    if (m.kind==ConfigMessageKind::Part && key==room.host &&
        (room.phase==Phase::HostConfig || room.phase==Phase::HostConfigAck)) {
        if (!room.config.insert(m)) return true;
        auto ack=m; ack.kind=ConfigMessageKind::Stored; ack.data.clear();
        socket_.send(client.address,encode_config_message(ack));
        if (room.phase==Phase::HostConfig && room.config.payload) {
            room.phase=Phase::HostConfigAck; room.ready={}; room.phaseStarted=Clock::now();
            for (auto& received:room.configReceived) received.assign(room.config.parts.size(),false);
            publish_phase(room);
        }
    } else if (room.phase==Phase::HostConfigAck && m.digest==room.config.digest) {
        if (m.kind==ConfigMessageKind::Received && m.index<room.configReceived[sender].size())
            room.configReceived[sender][m.index]=true;
        if (m.kind==ConfigMessageKind::Applied) {
            room.ready[sender]=true;
            if (room.ready[0] && room.ready[1]) {
                room.phase=Phase::Setup; room.ready={}; room.phaseStarted=Clock::now(); publish_phase(room);
            }
        }
    }
    return true;
}
void RelayServer::handle(const Datagram& datagram) {
    const auto& wire = datagram.data;
    if (wire == "!probe") { socket_.send(datagram.address, "!probe-ok"); return; }
    const auto key = sender_key(datagram.address);
    auto found = clients_.find(key);
    if (wire.rfind("!join;", 0) == 0 || wire.rfind("!join-v2;",0)==0) {
        const bool capable=wire.rfind("!join-v2;",0)==0;
        const auto token = wire.substr(capable?9:6);
        if (token.empty() || token.size() > 64 || token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") != std::string::npos) return;
        if (found == clients_.end()) {
            if (clients_.size() >= 256) { socket_.send(datagram.address, "!full;" + token); return; }
            found = clients_.emplace(key, Client{datagram.address, token, Clock::now(), {}, 0, {}}).first;
        } else if (found->second.token != token) {
            leave_room(found->second);
            found->second = Client{datagram.address, token, Clock::now(), {}, 0, {}};
        }
        if (found->second.username.empty()) {
            found->second.username = "GUEST-";
            constexpr char symbols[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            for (int i = 0; i < 8; ++i) found->second.username += symbols[random_() % 36];
        }
        found->second.lastSeen = Clock::now();
        found->second.roomConfigCapable=capable;
        socket_.send(datagram.address, "!joined;" + token);
        return;
    }
    if (wire.rfind("!ping;", 0) == 0) {
        const auto token = wire.substr(6);
        if (found == clients_.end() || found->second.token != token) {
            socket_.send(datagram.address, "!expired;" + token); return;
        }
        found->second.lastSeen = Clock::now();
        socket_.send(datagram.address, "!pong;" + token);
        if (!found->second.room.empty()) publish_room(found->second.room, rooms_.at(found->second.room));
        return;
    }
    if (found == clients_.end()) return;
    auto& client = found->second;
    if (handle_config(client,key,wire)) {client.lastSeen=Clock::now(); return;}
    if (wire == "!leave;" + client.token) { leave_room(client); clients_.erase(found); return; }
    if (wire.rfind("!username;",0) == 0) {
        if (const auto update = decode_username_update(wire); update && update->token == client.token) {
            client.lastSeen = Clock::now();
            const bool changed = update->revision > client.usernameRevision && client.username != update->username;
            if (update->revision > client.usernameRevision) {
                client.username = update->username; client.usernameRevision = update->revision;
            }
            socket_.send(client.address,encode_username_update({client.token,client.username,client.usernameRevision},true));
            if (changed && !client.room.empty()) publish_room(client.room,rooms_.at(client.room));
        }
        return;
    }
    if (wire.rfind("!room;", 0) == 0) {
        if (auto request = decode_room_request(wire); request && request->token == client.token) {
            client.lastSeen = Clock::now(); handle_room(client, *request);
        }
        return;
    }
    // Board snapshots and piece turns also use !. Keep routing them while
    // excluding unrelated connection/room commands from gameplay.
    if ((wire.rfind("!", 0) == 0 && !is_gameplay_extension(wire)) || client.room.empty()) return;
    client.lastSeen = Clock::now();
    auto& room = rooms_.at(client.room);
    const int sender = room.players[0] == key ? 0 : 1;
    const auto signal = decode_client_lobby_signal(wire);
    if (signal && *signal == ClientLobbySignal::Cancel) { reset_round(room, sender); return; }
    if (room.players[0].empty() || room.players[1].empty()) return;
    const auto advance = [&](Phase phase) {
        room.phase = phase; room.ready = {}; room.phaseStarted = Clock::now(); publish_phase(room);
    };
    if (room.phase == Phase::Waiting && signal && *signal == ClientLobbySignal::ReadyForConfig) {
        room.ready[sender] = true;
        if (room.ready[0] && room.ready[1]) {
            room.setup = {}; room.seed = encode_lobby_seed(random_());
            if (room.policy.forceRules || room.policy.forceHandling) {
                if (!clients_.at(room.players[0]).roomConfigCapable || !clients_.at(room.players[1]).roomConfigCapable) {
                    for (int i=0;i<2;++i) send_wire(room,i,"!config-error;Update all players before using forced host configuration.");
                    room.ready={}; return;
                }
                room.configEpoch=++nextConfigEpoch_; room.config={}; room.configReceived={}; room.configCursor={}; advance(Phase::HostConfig);
            } else advance(Phase::Setup);
        }
    } else if (room.phase == Phase::Setup) {
        if (auto setup = decode_lobby_player_setup(wire)) {
            setup->playerIndex = sender; room.setup[sender] = encode_lobby_player_setup(*setup);
            if (!room.setup[0].empty() && !room.setup[1].empty()) {
                auto first=decode_lobby_player_setup(room.setup[0]);
                auto second=decode_lobby_player_setup(room.setup[1]);
                // Only advertise the extension when both peers support it.
                // Strip it for older clients so their strict setup decoder works.
                if (first && second && !(first->hybridBoardSync && second->hybridBoardSync)) {
                    first->hybridBoardSync=second->hybridBoardSync=false;
                    room.setup[0]=encode_lobby_player_setup(*first); room.setup[1]=encode_lobby_player_setup(*second);
                }
                advance(Phase::SetupAck);
            }
        }
    } else if (room.phase == Phase::SetupAck && signal && *signal == ClientLobbySignal::SetupReceived) {
        room.ready[sender] = true;
        if (room.ready[0] && room.ready[1]) advance(Phase::Countdown);
    } else if (room.phase == Phase::Countdown && signal && *signal == ClientLobbySignal::CountdownComplete) {
        room.ready[sender] = true;
        if (room.ready[0] && room.ready[1]) { room.active = {}; advance(Phase::Playing); }
    } else if (room.phase == Phase::Playing) {
        if (signal || decode_lobby_player_setup(wire)) return;
        room.active[sender] = true; send_wire(room, 1-sender, wire);
        if (decode_round_signal(wire)) { room.phase = Phase::RoundAck; room.ready = {}; room.phaseStarted = Clock::now(); }
    } else if (room.phase == Phase::RoundAck) {
        if (is_round_acknowledgement(wire)) room.ready[sender] = true;
        if (decode_round_signal(wire)) send_wire(room, 1-sender, wire);
        if (room.ready[0] && room.ready[1]) { room.active = {}; room.setup = {}; advance(Phase::Waiting); }
    }
}
void RelayServer::poll(std::chrono::milliseconds timeout) {
    const auto now = Clock::now();
    for (auto it = clients_.begin(); it != clients_.end();) {
        if (now - it->second.lastSeen > std::chrono::seconds(5)) {
            leave_room(it->second); it = clients_.erase(it);
        } else ++it;
    }
    for (auto& [name, room] : rooms_) {
        if (room.phase == Phase::RoundAck && now - room.phaseStarted > std::chrono::seconds(2)) reset_round(room);
        const auto interval=(room.phase==Phase::HostConfig || room.phase==Phase::HostConfigAck)
            ? std::chrono::milliseconds(25) : std::chrono::milliseconds(250);
        if (now - room.lastPublish >= interval) publish_phase(room);
    }
    if (auto datagram = socket_.receive_for(timeout)) handle(*datagram);
}
[[noreturn]] void RelayServer::run() { for (;;) poll(); }
} // namespace Network
