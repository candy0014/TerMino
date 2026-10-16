#include "tetr/network/relay_server.h"
#include "tetr/network/frame_packet.h"
#include "tetr/network/network_services.h"
#include "tetr/network/event_stream.h"
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Network {
RelayServer::RelayServer(std::uint32_t randomSeed,const std::string& roomCodeHistory) : random_(randomSeed),roomCodeHistory_(roomCodeHistory),
    nextRoomInstance_(static_cast<std::uint64_t>(randomSeed)<<32),nextConfigEpoch_(static_cast<std::uint64_t>(randomSeed)<<32),
    nextMatchEpoch_(static_cast<std::uint64_t>(randomSeed)<<32) {
    if(!roomCodeHistory_.empty()) {
        const auto path=std::filesystem::u8path(roomCodeHistory_);
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        std::ifstream input(path);std::string code;
        if(!input && std::filesystem::exists(path))throw std::runtime_error("Cannot read room code history.");
        while(std::getline(input,code))if(code.size()==4 && code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")==std::string::npos)usedRoomCodes_.insert(code);
        std::ofstream output(path,std::ios::app);
        if(!output)throw std::runtime_error("Cannot write room code history.");
    }
}
void RelayServer::remember_room_code(const std::string& name) {
    if(name.size()!=4 || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")!=std::string::npos || usedRoomCodes_.count(name))return;
    if(!roomCodeHistory_.empty()) {
        std::ofstream output(std::filesystem::u8path(roomCodeHistory_),std::ios::app);output<<name<<'\n';output.flush();
        if(!output)throw std::runtime_error("Cannot save room code history.");
    }
    usedRoomCodes_.insert(name);
}
std::string RelayServer::reserve_room_name(Client& client) {
    if(!client.reservedRoomName.empty())return client.reservedRoomName;
    constexpr int count=26*26*26*26;
    const auto code=[](int value) {std::string name(4,'A');for(int i=3;i>=0;--i){name[i]+=value%26;value/=26;}return name;};
    std::uniform_int_distribution<int> draw(0,count-1);
    for(int attempt=0;attempt<64;++attempt) {
        const auto name=code(draw(random_));
        if(!rooms_.count(name) && !usedRoomCodes_.count(name)) {remember_room_code(name);return client.reservedRoomName=name;}
    }
    const int start=draw(random_);
    for(int offset=0;offset<count;++offset) {
        const auto name=code((start+offset)%count);
        if(!rooms_.count(name) && !usedRoomCodes_.count(name)) {remember_room_code(name);return client.reservedRoomName=name;}
    }
    return {};
}
void RelayServer::bind(UdpPort port) { socket_.bind(port); socket_.set_non_blocking(); }
std::string RelayServer::sender_key(const UdpAddress& address) const {
    return address.ip + ":" + std::to_string(address.port);
}
void RelayServer::send_wire(const Room& room, int index, const std::string& wire) {
    const auto client = clients_.find(room.players[index]);
    if (client != clients_.end()) socket_.send(client->second.address, wire);
}
void RelayServer::send_mini_batch(const Room& room,int index,const std::vector<std::string>& events) {
    const auto client=clients_.find(room.players[index]);
    if(client==clients_.end())return;
    if(!client->second.relayBatchCapable) {
        for(const auto& wire:events)socket_.send(client->second.address,wire);
        return;
    }
    // The mini scheduler already selected these images together. Bundle only
    // that ready work, with no timer and no extra wait for another image.
    std::vector<std::string> batch;std::size_t size=6;
    const auto flush=[&] {
        if(batch.empty())return;
        if(batch.size()==1)socket_.send(client->second.address,batch.front());
        else socket_.send(client->second.address,*encode_event_stream(batch));
        batch.clear();size=6;
    };
    for(const auto& wire:events) {
        if(size+4+wire.size()>kEventStreamMaxPacket)flush();
        // Keep a maximum-sized legacy packet intact rather than wrapping it.
        if(6+4+wire.size()>kEventStreamMaxPacket)socket_.send(client->second.address,wire);
        else {size+=4+wire.size();batch.push_back(wire);}
    }
    flush();
}
void RelayServer::reset_round(Room& room, int except) {
    if (room.phase != Phase::Waiting)
        for (int i = 0; i < room.capacity; ++i) if (i != except)
            send_wire(room, i, room.multiplayer?"!match-canceled;"+std::to_string(room.matchEpoch):"!round-reset");
    room.phase = Phase::Waiting;
    room.startRequested=false;
    room.ready = {}; room.active = {}; room.setup = {};
    room.config={}; room.configReceived={}; room.configCursor={};
    room.entered={};room.configApplied={};room.participating={};room.alive={};room.snapshots={};
    room.miniProjections={};
    room.pendingAttacks={};room.winner=-1;
    room.lastPublish = {}; room.phaseStarted = Clock::now();
}
void RelayServer::publish_phase(Room& room) {
    if(room.multiplayer) {publish_multiplayer_phase(room);return;}
    if (room.players[0].empty() || room.players[1].empty()) return;
    for (int i = 0; i < 2; ++i) {
        switch (room.phase) {
            case Phase::Preparing: break;
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
        if (client != clients_.end()) members.push_back({static_cast<int>(slot),client->second.username,room.host == client->first,
            room.roles[slot],room.ready[slot],room.participating[slot],room.alive[slot],room.killer[slot],room.target[slot]});
    }
    return members;
}
void RelayServer::publish_room(const std::string& name, Room& room,int onlySlot) {
    const auto members=room_members(room);
    const int count = static_cast<int>(members.size());
    for (int i = 0; i < room.capacity; ++i) {
        if(onlySlot>=0 && i!=onlySlot) continue;
        const auto client = clients_.find(room.players[i]);
        if (client != clients_.end()) {
            RoomResponse state{client->second.token,0,RoomResult::Joined,{name,room.isPublic,count,room.memberLimit},i,{},++client->second.stateRevision,members};
            state.policy=room.policy; state.hasPolicy=client->second.roomConfigCapable;
            state.roomInstance=room.instance;
            state.multiplayer=room.multiplayer;state.matchPhase=match_phase(room);state.matchEpoch=room.matchEpoch;
            state.battleLimit=std::min(room.memberLimit,room.battleLimit);
            send_wire(room,i,encode_room_response(state));
        }
    }
}
void RelayServer::leave_room(Client& client) {
    const auto found = rooms_.find(client.room);
    if (found == rooms_.end()) { client.room.clear(); return; }
    auto& room = found->second;
    for (int i = 0; i < room.capacity; ++i) if (room.players[i] == sender_key(client.address)) {
        if(room.multiplayer && (room.phase==Phase::Playing || room.phase==Phase::RoundAck || room.phase==Phase::Countdown)) eliminate_player(room,i);
        else if(!room.multiplayer || room.host==sender_key(client.address)) reset_round(room, i);
        // Also interrupt a player who was waiting before the round started.
        if(!room.multiplayer) send_wire(room, 1-i, "!round-reset");
        room.players[i].clear();
        room.countdownClockCapable[i]=false;
        room.ready[i]=room.entered[i]=room.configApplied[i]=room.participating[i]=room.alive[i]=false;
        room.setup[i].clear();room.snapshots[i].clear();room.roles[i]=PlayerRole::Battle;
        room.boardViews[i].reset();room.miniBoards[i].reset();room.miniProjections[i].clear();room.boardBudgets[i].clear();
        room.previewReceived[i].clear();room.previewCursor[i]=0;
        if (room.host == sender_key(client.address)) {
            room.preview={};room.previewReceived={};room.previewCursor={};
            std::vector<std::string> candidates;
            for (const auto& player : room.players) if (!player.empty()) candidates.push_back(player);
            room.host = candidates.empty() ? std::string{} : candidates[std::uniform_int_distribution<std::size_t>(0,candidates.size()-1)(random_)];
        }
    }
    client.room.clear();
    if (room_members(room).empty()) rooms_.erase(found);
    else { if(room.multiplayer) {select_targets(room,true);start_ready_players(room,false);} publish_room(found->first, room); }
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
            reply.rooms.push_back({key, true, static_cast<int>(room_members(room).size()),room.memberLimit});
    } else if(request.operation==RoomOperation::SuggestName) {
        if(!client.room.empty())reply.result=RoomResult::AlreadyInRoom;
        else {
            const auto suggested=reserve_room_name(client);
            reply.result=suggested.empty()?RoomResult::Busy:RoomResult::NameSuggested;
            reply.room={suggested,true,0,client.multiplayerCapable?kMultiplayerRoomCapacity:2};
        }
    } else if (request.operation == RoomOperation::Leave) {
        leave_room(client);client.evictionResponse.clear(); reply.result = RoomResult::Left;
    } else if(request.operation==RoomOperation::Kick) {
        const auto found=rooms_.find(client.room);
        if(found==rooms_.end()) reply.result=RoomResult::NotFound;
        else if(found->second.host!=sender_key(client.address)) reply.result=RoomResult::HostOnly;
        else {
            auto& room=found->second;
            const auto target=request.targetSlot;
            const auto victim=target<0 || target>=room.capacity ? clients_.end() : clients_.find(room.players[target]);
            if(victim==clients_.end() || victim==clients_.find(sender_key(client.address)) ||
               (!request.name.empty() && victim->second.username!=request.name)) reply.result=RoomResult::InvalidPlayer;
            else {
                auto& removed=victim->second;
                leave_room(removed);
                // Unsolicited Left is also understood by older clients.
                RoomResponse departure{removed.token,0,RoomResult::Left,{},-1,{},++removed.stateRevision,{}};
                removed.evictionResponse=encode_room_response(departure);
                socket_.send(removed.address,removed.evictionResponse);
                if(!removed.multiplayerCapable) socket_.send(removed.address,"!round-reset");
                reply.result=RoomResult::PlayerKicked;
            }
        }
    } else if(request.operation>=RoomOperation::SetRole) {
        const auto found=rooms_.find(client.room);
        if(found==rooms_.end()) reply.result=RoomResult::NotFound;
        else reply.result=handle_multiplayer_request(found->second,client,request);
    } else if (request.operation==RoomOperation::TransferHost || request.operation==RoomOperation::SetPolicy) {
        const auto found=rooms_.find(client.room);
        if (found==rooms_.end()) reply.result=RoomResult::NotFound;
        else {
            auto& room=found->second;
            if (room.host!=sender_key(client.address)) reply.result=RoomResult::HostOnly;
            else if (room.phase!=Phase::Waiting && !(room.multiplayer &&
                     (request.operation==RoomOperation::TransferHost || room.phase==Phase::RoundAck)) &&
                     !(request.operation==RoomOperation::SetPolicy && room.manualPreparation && room.phase==Phase::Preparing)) reply.result=RoomResult::RoundBusy;
            else if (request.operation==RoomOperation::SetPolicy) {
                const bool changed=room.policy!=request.policy;
                if(changed) {room.preview={};room.previewReceived={};room.previewCursor={};}
                room.policy=request.policy;
                if(room.manualPreparation && room.phase==Phase::Preparing) {
                    if(changed)restart_multiplayer_preparation(room);
                } else room.ready={};
                reply.result=RoomResult::PolicyChanged;
            } else if (request.targetSlot<0 || request.targetSlot>=static_cast<int>(room.players.size()) || room.players[request.targetSlot].empty() ||
                (!request.name.empty() && clients_.at(room.players[request.targetSlot]).username!=request.name))
                reply.result=RoomResult::InvalidPlayer;
            else {
                if(room.host!=room.players[request.targetSlot]) {
                    room.preview={};room.previewReceived={};room.previewCursor={};
                    room.host=room.players[request.targetSlot];
                    // A new host supplies the next rules. Keep entrants ready,
                    // but discard setup/configuration derived from the old host.
                    if(room.multiplayer && room.phase==Phase::Preparing)
                        restart_multiplayer_preparation(room);
                    else if(!room.multiplayer)room.ready={};
                }
                reply.result=RoomResult::HostTransferred;
            }
        }
    } else if (!client.room.empty()) {
        reply.result = RoomResult::AlreadyInRoom;
    } else if (!valid_room_name(name, request.operation == RoomOperation::Create)) {
        reply.result = RoomResult::InvalidName;
    } else {
        auto found = rooms_.find(name);
        if (request.operation == RoomOperation::Create) {
            if (name.empty()) {
                name=reserve_room_name(client);
                if(name.empty()) {
                    reply.result=RoomResult::Busy;reply.stateRevision=++client.stateRevision;client.lastRequest=request.id;
                    client.lastResponse=encode_room_response(reply);socket_.send(client.address,client.lastResponse);return;
                }
                found = rooms_.end();
            }
            const bool reservedElsewhere=std::any_of(clients_.begin(),clients_.end(),[&](const auto& peer){return &peer.second!=&client && peer.second.reservedRoomName==name;});
            if (found != rooms_.end() || reservedElsewhere) reply.result = RoomResult::NameTaken;
            else {
                remember_room_code(name);client.reservedRoomName.clear();
                found = rooms_.try_emplace(name).first;
                found->second.instance=++nextRoomInstance_;
                found->second.isPublic = request.isPublic;
                found->second.multiplayer=client.multiplayerCapable;
                found->second.capacity=client.multiplayerCapable?kMultiplayerRoomCapacity:2;
                found->second.memberLimit=found->second.battleLimit=found->second.capacity;
                found->second.killer.fill(-1);found->second.target.fill(-1);found->second.lastAttacker.fill(-1);
                reply.result = RoomResult::Created;
            }
        } else reply.result = found == rooms_.end() ? RoomResult::NotFound : RoomResult::Joined;
        if (reply.result == RoomResult::Created || reply.result == RoomResult::Joined) {
            auto& room = found->second;
            const auto available=std::find(room.players.begin(),room.players.begin()+room.capacity,std::string{});
            const int slot=available==room.players.begin()+room.capacity?-1:static_cast<int>(available-room.players.begin());
            if(room.multiplayer && !client.multiplayerCapable) reply.result=RoomResult::Incompatible;
            else if (slot < 0 || static_cast<int>(room_members(room).size())>=room.memberLimit) reply.result = RoomResult::Full;
            else {
                room.players[slot] = sender_key(client.address); client.room = name;client.evictionResponse.clear();client.reservedRoomName.clear();
                const int battles=static_cast<int>(std::count_if(room.players.begin(),room.players.end(),[&](const auto& key) {
                    const auto index=&key-room.players.data();return !key.empty() && index!=slot && room.roles[index]==PlayerRole::Battle;
                }));
                room.roles[slot]=room.multiplayer && (room.phase!=Phase::Waiting || battles>=std::min(room.memberLimit,room.battleLimit))?PlayerRole::Spectator:PlayerRole::Battle;
                room.previewReceived[slot].clear();room.previewCursor[slot]=0;
                if (room.host.empty()) room.host = room.players[slot];
                reply.members = room_members(room);
                reply.room = {name, room.isPublic, static_cast<int>(room_members(room).size()),room.memberLimit};
                reply.playerIndex = slot;
            }
        }
    }
    if (room_result_has_state(reply.result) && !client.room.empty()) {
        const auto& room=rooms_.at(client.room);
        reply.room={client.room,room.isPublic,static_cast<int>(room_members(room).size()),room.memberLimit};
        reply.members=room_members(room);
        reply.playerIndex=static_cast<int>(std::find(room.players.begin(),room.players.end(),sender_key(client.address))-room.players.begin());
    }
    reply.hasPolicy=client.roomConfigCapable;
    if (!client.room.empty()) {
        const auto& room=rooms_.at(client.room);reply.policy=room.policy;reply.roomInstance=room.instance;
        reply.multiplayer=room.multiplayer && client.multiplayerCapable && room_result_has_state(reply.result);reply.matchPhase=match_phase(room);reply.matchEpoch=room.matchEpoch;
        reply.battleLimit=std::min(room.memberLimit,room.battleLimit);
    }
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
    if (wire.rfind("!config-",0)!=0 && wire.rfind("!preview-",0)!=0) return false;
    const auto message=decode_config_message(wire);
    if (!message || message->token!=client.token || client.room.empty()) return true;
    auto& room=rooms_.at(client.room);
    const auto& m=*message;
    if(m.preview) {
        auto& preview=room.preview;
        const auto sender=static_cast<int>(std::find(room.players.begin(),room.players.end(),key)-room.players.begin());
        if(!room.policy.forceRules || sender>=static_cast<int>(room.players.size())) return true;
        if(m.kind==ConfigMessageKind::Begin && key==room.host && m.roomInstance==room.instance && m.hostSlot==sender && m.epoch>preview.epoch) {
            preview={};preview.epoch=m.epoch;preview.hostSlot=sender;
            // Receiver epochs must stay increasing across host changes, even
            // when the new host's own upload revision starts again at one.
            room.previewBroadcastEpoch=++nextConfigEpoch_;
            room.previewReceived={};room.previewCursor={};room.lastPreviewPublish={};
        }
        if(m.kind==ConfigMessageKind::Received) {
            if(m.epoch==room.previewBroadcastEpoch && m.digest==preview.incoming.digest && m.index<room.previewReceived[sender].size())
                room.previewReceived[sender][m.index]=true;
            return true;
        }
        if(m.epoch!=preview.epoch || !preview.epoch) return true;
        if(m.kind==ConfigMessageKind::Part && key==room.host && preview.incoming.insert(m)) {
            auto ack=m;ack.kind=ConfigMessageKind::Stored;ack.data.clear();socket_.send(client.address,encode_config_message(ack));
        }
        return true;
    }
    if (m.epoch!=room.configEpoch) return true;
    const auto sender=static_cast<int>(std::find(room.players.begin(),room.players.end(),key)-room.players.begin());
    if (sender>=static_cast<int>(room.players.size())) return true;
    if(room.multiplayer) {
        if(room.phase==Phase::Waiting || !room.policy.forceRules) return true;
        bool changed=false;
        const bool hadPayload=room.config.payload.has_value();
        if(m.kind==ConfigMessageKind::Part && key==room.host && room.phase==Phase::Preparing && room.config.insert(m)) {
            auto ack=m;ack.kind=ConfigMessageKind::Stored;ack.data.clear();socket_.send(client.address,encode_config_message(ack));
            changed=!hadPayload && room.config.payload.has_value();
        }
        if(m.digest==room.config.digest && room.config.payload) {
            if(m.kind==ConfigMessageKind::Received && m.index<room.configReceived[sender].size()) room.configReceived[sender][m.index]=true;
            if(m.kind==ConfigMessageKind::Applied && room.entered[sender] && !room.configApplied[sender]) {
                room.configApplied[sender]=true;changed=true;
            }
        }
        if(changed)publish_multiplayer_phase(room);
        return true;
    }
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
    if (wire.rfind("!join;", 0) == 0 || wire.rfind("!join-v2;",0)==0 || wire.rfind("!join-v3;",0)==0 || wire.rfind("!join-v4;",0)==0) {
        const bool modern=wire.rfind("!join-v4;",0)==0;
        const bool capable=wire.rfind("!join-v3;",0)==0 || modern;
        const auto token = wire.substr(wire.rfind("!join;",0)==0?6:9);
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
        found->second.multiplayerCapable=modern;
        socket_.send(datagram.address, "!joined;" + token);
        return;
    }
    if(wire.rfind("!relay-batch-v1;",0)==0) {
        if(found!=clients_.end() && wire.substr(16)==found->second.token)
            found->second.relayBatchCapable=true;
        return;
    }
    if (wire.rfind("!ping;", 0) == 0) {
        const auto token = wire.substr(6);
        if (found == clients_.end() || found->second.token != token) {
            socket_.send(datagram.address, "!expired;" + token); return;
        }
        found->second.lastSeen = Clock::now();
        socket_.send(datagram.address, "!pong;" + token);
        if (!found->second.room.empty()) {
            auto& room=rooms_.at(found->second.room);
            const int slot=static_cast<int>(std::find(room.players.begin(),room.players.end(),key)-room.players.begin());
            publish_room(found->second.room,room,slot);
        } else if(!found->second.evictionResponse.empty()) {
            socket_.send(datagram.address,found->second.evictionResponse);
            if(!found->second.multiplayerCapable) socket_.send(datagram.address,"!round-reset");
        }
        return;
    }
    if (found == clients_.end()) return;
    auto& client = found->second;
    if (handle_config(client,key,wire)) {client.lastSeen=Clock::now(); return;}
    if (wire == "!leave;" + client.token) {
        std::clog<<"Client disconnected: explicit leave; room="<<client.room<<" endpoint="<<key<<'\n';
        leave_room(client); clients_.erase(found); return;
    }
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
    if(!client.room.empty() && rooms_.at(client.room).multiplayer) {
        auto& room=rooms_.at(client.room);
        const int slot=static_cast<int>(std::find(room.players.begin(),room.players.end(),key)-room.players.begin());
        if(slot<room.capacity) {client.lastSeen=Clock::now();handle_multiplayer_gameplay(room,client,slot,wire);}
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
            if (room.policy.forceRules) {
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
void RelayServer::publish_preview(Room& room) {
    auto& preview=room.preview;
    if(!room.policy.forceRules || !preview.epoch || !preview.incoming.payload) return;
    const auto count=preview.incoming.parts.size();
    for(std::size_t slot=0;slot<room.players.size();++slot) {
        const auto client=clients_.find(room.players[slot]);if(client==clients_.end() || !client->second.roomConfigCapable || room.players[slot]==room.host) continue;
        if(room.previewReceived[slot].size()!=count) room.previewReceived[slot].assign(count,false);
        if(std::all_of(room.previewReceived[slot].begin(),room.previewReceived[slot].end(),[](bool received){return received;})) continue;
        ConfigMessage begin;begin.preview=true;begin.token=client->second.token;begin.epoch=room.previewBroadcastEpoch;
        begin.roomInstance=room.instance;begin.hostSlot=preview.hostSlot;begin.policy=room.policy;
        send_wire(room,static_cast<int>(slot),encode_config_message(begin));
        std::size_t scanned=0,sent=0;
        while(scanned++<count && sent<32) {
            const auto index=room.previewCursor[slot]++%count;
            if(room.previewReceived[slot][index]) continue;
            auto part=room_config_part(*preview.incoming.payload,client->second.token,room.previewBroadcastEpoch,preview.incoming.digest,index);
            part.preview=true;send_wire(room,static_cast<int>(slot),encode_config_message(part));++sent;
        }
    }
    room.lastPreviewPublish=Clock::now();
}
void RelayServer::poll(std::chrono::milliseconds timeout) {
    const auto now = Clock::now();
    for (auto it = clients_.begin(); it != clients_.end();) {
        if (now - it->second.lastSeen > kConnectedTimeout) {
            std::clog<<"Client disconnected: heartbeat timeout; room="<<it->second.room<<" endpoint="<<it->first<<'\n';
            leave_room(it->second); it = clients_.erase(it);
        } else ++it;
    }
    for (auto& [name, room] : rooms_) {
        if(room.manualPreparation && room.startRequested)start_ready_players(room,true);
        flush_mini_boards(room,now);
        if(room.phase==Phase::Playing && std::any_of(room.boardViews.begin(),room.boardViews.end(),[](const auto& view){return view.has_value();}))
            timeout=std::min(timeout,std::chrono::milliseconds(8));
        advance_scheduled_countdown(room,name);
        if(now-room.lastPreviewPublish>=std::chrono::milliseconds(25)) publish_preview(room);
        if (!room.multiplayer && room.phase == Phase::RoundAck && now - room.phaseStarted > std::chrono::seconds(2)) reset_round(room);
        const auto interval=(room.phase==Phase::HostConfig || room.phase==Phase::HostConfigAck || (room.multiplayer && room.phase==Phase::Preparing))
            ? std::chrono::milliseconds(25) : std::chrono::milliseconds(250);
        if (now - room.lastPublish >= interval) publish_phase(room);
    }
    if (auto datagram = socket_.receive_for(timeout)) handle(*datagram);
}
[[noreturn]] void RelayServer::run() { for (;;) poll(); }
} // namespace Network
