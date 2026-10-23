#include "tetr/network/relay_server.h"
#include "tetr/util/text_codec.h"

#include <algorithm>
#include <limits>

namespace Network {
RoomMatchPhase RelayServer::match_phase(const Room& room) const {
    if(room.phase==Phase::Waiting) return RoomMatchPhase::Idle;
    if(room.phase==Phase::Countdown) return RoomMatchPhase::Countdown;
    if(room.phase==Phase::Playing) return RoomMatchPhase::Playing;
    if(room.phase==Phase::RoundAck) return RoomMatchPhase::Finished;
    return RoomMatchPhase::Preparing;
}

void RelayServer::begin_multiplayer_match(Room& room) {
    finish_room_replay(room,Replay::RoundEndReason::HostStopped);
    if(room.matchEpoch && (room.phase==Phase::Playing || room.phase==Phase::RoundAck)) {
        Room::ResultHistory history;history.epoch=room.matchEpoch;
        history.results=std::move(room.results);history.uploads=std::move(room.resultUploads);
        for(int slot=0;slot<room.capacity;++slot) if(room.participating[slot])history.actors[slot]=room.players[slot];
        room.previousResults=std::move(history);
    }
    room.results={};room.resultUploads={};room.startRequested=false;
    // A replacement round is announced by its new epoch. Sending a cancel
    // first can make peers open settings before they receive the new roster.
    room.matchEpoch=++nextMatchEpoch_;
    room.phase=Phase::Preparing;room.phaseStarted=Clock::now();room.lastPublish={};room.lastSetupPublish={};room.lastConfigPublish={};
    room.seed=encode_lobby_seed(random_());room.configEpoch=++nextConfigEpoch_;
    room.config={};room.configReceived={};room.configCursor={};
    room.ready={};room.entered={};room.configApplied={};room.setup={};room.snapshots={};
    room.boardViews={};room.boardBudgets={};room.miniBoards={};room.miniProjections={};room.miniBoardReceived={};room.lastMiniForward={};
    room.miniForwardSequence={};room.lastMiniBatch={};room.miniBatchCursor={};
    room.participating={};room.alive={};room.active={};room.pendingAttacks={};
    for(int slot=0;slot<room.capacity;++slot)
        room.participating[slot]=!room.players[slot].empty() && room.roles[slot]==PlayerRole::Battle;
    room.seenAttacks={};room.deliveredAttacks={};room.attackAckCursor={};room.attackSources={};room.attackTargets={};
    room.sentAttackId={};room.receivedAttackId={};room.target.fill(-1);room.lastAttacker.fill(-1);room.killer.fill(-1);
    room.lastTargetSelection={};room.winner=-1;
}

void RelayServer::restart_multiplayer_preparation(Room& room) {
    const auto entered=room.entered,ready=room.ready;
    begin_multiplayer_match(room);
    room.entered=entered;room.ready=ready;
}

bool RelayServer::start_ready_players(Room& room,bool force) {
    if(!room.multiplayer || room.phase!=Phase::Preparing) return false;
    if(room.manualPreparation && !force && !room.startRequested)return false;
    int battles=0,ready=0;
    for(int slot=0;slot<room.capacity;++slot) if(room.participating[slot]) {
        ++battles;if(room.ready[slot]) ++ready;
    }
    if(ready<2 || (!force && ready!=battles)) return false;
    if(room.manualPreparation)for(int slot=0;slot<room.capacity;++slot)
        if(room.participating[slot] && room.ready[slot] &&
           (!room.entered[slot] || room.setup[slot].empty() || (room.policy.forceRules && !room.configApplied[slot])))return false;
    for(int slot=0;slot<room.capacity;++slot) {
        room.participating[slot]=room.participating[slot] && room.ready[slot];
        room.alive[slot]=room.participating[slot];
    }
    room.phase=Phase::Countdown;room.startRequested=false;room.active={};room.phaseStarted=Clock::now();room.lastPublish={};
    room.scheduledCountdown=true;
    for(int slot=0;slot<room.capacity;++slot)
        if(room.participating[slot] && !room.countdownClockCapable[slot]) room.scheduledCountdown=false;
    select_targets(room,true);
    for(int slot=0;slot<room.capacity;++slot) {
        if(room.players[slot].empty()) continue;
        publish_room(clients_.at(room.players[slot]).room,room);break;
    }
    publish_multiplayer_phase(room);return true;
}

RoomResult RelayServer::handle_multiplayer_request(Room& room,Client& client,const RoomRequest& request) {
    if(!room.multiplayer || !client.multiplayerCapable) return RoomResult::Incompatible;
    const auto key=sender_key(client.address);
    const int slot=static_cast<int>(std::find(room.players.begin(),room.players.end(),key)-room.players.begin());
    if(slot>=room.capacity) return RoomResult::InvalidPlayer;
    if(request.operation==RoomOperation::Prepare) {
        if(room.phase==Phase::Playing || room.phase==Phase::Countdown)return RoomResult::RoundBusy;
        if(room.roles[slot]==PlayerRole::Offline) {
            if(room.host!=key)return RoomResult::OfflineRestricted;
            room.roles[slot]=PlayerRole::Spectator;
        }
        if(room.phase!=Phase::Preparing)begin_multiplayer_match(room);
        room.manualPreparation=true;
        room.entered[slot]=true;
        room.participating[slot]=room.roles[slot]==PlayerRole::Battle;
        room.ready[slot]=room.participating[slot];
        publish_multiplayer_phase(room);
        return RoomResult::ReadyChanged;
    }
    if(request.operation==RoomOperation::EndMatch) {
        if(room.host!=key)return RoomResult::HostOnly;
        if(request.matchEpoch!=room.matchEpoch || (room.phase!=Phase::Playing && room.phase!=Phase::Countdown))return RoomResult::RoundBusy;
        const auto entered=room.entered;
        begin_multiplayer_match(room);room.manualPreparation=true;
        for(int peer=0;peer<room.capacity;++peer)if(entered[peer] && room.roles[peer]!=PlayerRole::Offline) {
            room.entered[peer]=true;room.ready[peer]=room.roles[peer]==PlayerRole::Battle;
        }
        publish_multiplayer_phase(room);
        return RoomResult::MatchStarted;
    }
    if(request.operation==RoomOperation::SetRole || request.operation==RoomOperation::SetMemberRole) {
        int target=slot;
        PlayerRole role;
        if(request.operation==RoomOperation::SetMemberRole) {
            if(room.host!=key) return RoomResult::HostOnly;
            target=request.targetSlot;role=request.memberRole;
            if(target<0 || target>=room.capacity || room.players[target].empty() ||
               static_cast<int>(role)<0 || static_cast<int>(role)>static_cast<int>(PlayerRole::Offline)) return RoomResult::InvalidPlayer;
            if(!request.name.empty() && clients_.at(room.players[target]).username!=request.name) return RoomResult::InvalidPlayer;
        } else {
            if(request.targetSlot<0 || request.targetSlot>2) return RoomResult::InvalidPlayer;
            role=static_cast<PlayerRole>(request.targetSlot);
        }
        if(room.phase==Phase::Countdown && room.participating[target] && room.alive[target]) return RoomResult::RoundBusy;
        if(role!=room.roles[target]) {
            if(role==PlayerRole::Battle) {
                int battles=0;for(int peer=0;peer<room.capacity;++peer) if(!room.players[peer].empty() && room.roles[peer]==PlayerRole::Battle) ++battles;
                if(battles>=std::min(room.memberLimit,room.battleLimit)) return RoomResult::BattleFull;
            }
            if(room.phase==Phase::Playing && role!=PlayerRole::Battle) eliminate_player(room,target);
            room.roles[target]=role;room.ready[target]=false;room.startRequested=false;
            if(room.phase==Phase::Preparing) {
                if(room.manualPreparation)room.participating[target]=role==PlayerRole::Battle;
                else if(role!=PlayerRole::Battle)room.participating[target]=false;
                if(room.manualPreparation && room.entered[target])room.ready[target]=role==PlayerRole::Battle;
                start_ready_players(room,false);
            }
        }
        return RoomResult::RoleChanged;
    }
    if(request.operation==RoomOperation::SetReady) {
        if(request.targetSlot<0 || request.targetSlot>1 || request.matchEpoch!=room.matchEpoch)
            return RoomResult::RoundBusy;
        if(room.phase!=Phase::Preparing) return RoomResult::RoundBusy;
        if(request.targetSlot && (!room.participating[slot] || !room.entered[slot] ||
            (!room.manualPreparation && (room.setup[slot].empty() || (room.policy.forceRules && !room.configApplied[slot]))))) return RoomResult::RoundBusy;
        room.ready[slot]=request.targetSlot!=0;
        if(!room.ready[slot])room.startRequested=false;
        start_ready_players(room,false);return RoomResult::ReadyChanged;
    }
    if(room.host!=key) return RoomResult::HostOnly;
    if(request.operation==RoomOperation::SetLimits) {
        if(room.phase!=Phase::Waiting && room.phase!=Phase::RoundAck) return RoomResult::RoundBusy;
        if(request.targetSlot<2 || request.targetSlot>kMultiplayerRoomCapacity) return RoomResult::InvalidPlayer;
        if(request.targetSlot<static_cast<int>(room_members(room).size())) return RoomResult::LimitBelowMembers;
        room.memberLimit=request.targetSlot;room.battleLimit=std::clamp(request.battleLimit,2,room.memberLimit);
        int count=0;
        if(room.roles[slot]==PlayerRole::Battle) ++count;
        for(int peer=0;peer<room.capacity;++peer) if(peer!=slot && !room.players[peer].empty() && room.roles[peer]==PlayerRole::Battle)
            if(++count>room.battleLimit) room.roles[peer]=PlayerRole::Spectator;
        return RoomResult::LimitsChanged;
    }
    if(request.operation==RoomOperation::StartMatch) {
        room.manualPreparation=false;
        if(room.roles[slot]==PlayerRole::Offline) room.roles[slot]=PlayerRole::Spectator;
        if(room.phase!=Phase::Preparing) begin_multiplayer_match(room);
        return RoomResult::MatchStarted;
    }
    if(request.operation==RoomOperation::ForceStart) {
        if(room.manualPreparation) {
            int ready=0;for(int peer=0;peer<room.capacity;++peer)if(room.participating[peer] && room.ready[peer])++ready;
            if(request.matchEpoch!=room.matchEpoch || room.phase!=Phase::Preparing || ready<2)return RoomResult::NeedReadyPlayers;
            room.startRequested=true;start_ready_players(room,true);return RoomResult::MatchForced;
        }
        if(request.matchEpoch!=room.matchEpoch || !start_ready_players(room,true)) return RoomResult::NeedReadyPlayers;
        return RoomResult::MatchForced;
    }
    if(request.operation==RoomOperation::CancelMatch) {
        if(request.matchEpoch!=room.matchEpoch) return RoomResult::RoundBusy;
        reset_round(room);return RoomResult::MatchCanceled;
    }
    return RoomResult::Busy;
}

void RelayServer::select_targets(Room& room,bool force) {
    if(!room.multiplayer || (room.phase!=Phase::Playing && room.phase!=Phase::Countdown)) return;
    const auto now=Clock::now();
    if(!force && now-room.lastTargetSelection<std::chrono::seconds(5)) return;
    std::vector<int> slots;
    for(int slot=0;slot<room.capacity;++slot) if(room.alive[slot]) slots.push_back(slot);
    std::shuffle(slots.begin(),slots.end(),random_);
    room.target.fill(-1);
    // EVEN: distribute targets across the surviving battle players. A random
    // cycle avoids self-targets while giving each player one incoming target.
    if(slots.size()>1) for(std::size_t i=0;i<slots.size();++i) room.target[slots[i]]=slots[(i+1)%slots.size()];
    for(const int slot:slots)
        send_wire(room,slot,"!target;"+std::to_string(room.matchEpoch)+";"+std::to_string(room.target[slot]));
    room.lastTargetSelection=now;
    record_room_replay_targets(room);
}

void RelayServer::acknowledge_delivery(Room& room,int source,int sequence) {
    if(source<0 || source>=room.capacity || sequence<=room.attackAckCursor[source]) return;
    room.deliveredAttacks[source].insert(sequence);
    while(room.deliveredAttacks[source].erase(room.attackAckCursor[source]+1)) ++room.attackAckCursor[source];
    while(!room.seenAttacks[source].empty() && *room.seenAttacks[source].begin()<=room.attackAckCursor[source])
        room.seenAttacks[source].erase(room.seenAttacks[source].begin());
    while(!room.attackTargets[source].empty() && room.attackTargets[source].begin()->first<=room.attackAckCursor[source]) room.attackTargets[source].erase(room.attackTargets[source].begin());
    send_wire(room,source,"!attack-delivered;"+std::to_string(room.matchEpoch)+";"+std::to_string(room.attackAckCursor[source]));
}

void RelayServer::eliminate_player(Room& room,int slot) {
    if(!room.multiplayer || slot<0 || slot>=room.capacity || !room.alive[slot]) return;
    room.alive[slot]=false;
    room.killer[slot]=room.lastAttacker[slot];
    record_room_replay_elimination(room,slot);
    const std::string notice="!knockout;"+std::to_string(room.matchEpoch)+";"+
        std::to_string(slot)+";"+std::to_string(room.lastAttacker[slot]);
    for(int peer=0;peer<room.capacity;++peer) send_wire(room,peer,notice);
    for(const auto& delivery:room.pendingAttacks[slot]) acknowledge_delivery(room,delivery.source,delivery.sourceSequence);
    room.pendingAttacks[slot].clear();select_targets(room,true);
    update_board_budgets(room);
    int remaining=0,winner=-1;
    for(int peer=0;peer<room.capacity;++peer) if(room.alive[peer]) {++remaining;winner=peer;}
    if(remaining<2 && room.phase==Phase::Countdown) {reset_round(room);}
    else if(remaining<2 && room.phase==Phase::Playing) {
        room.winner=remaining?winner:-1;room.phase=Phase::RoundAck;room.ready={};room.startRequested=false;room.lastPublish={};room.phaseStarted=Clock::now();
        finish_room_replay(room);
    }
    for(const auto& key:room.players) if(!key.empty()) {publish_room(clients_.at(key).room,room);break;}
}

void RelayServer::update_board_budgets(Room& room,int resendSlot) {
    for(int source=0;source<room.capacity;++source)if(!room.players[source].empty()) {
        BoardBudget budget{false,0,0,0};
        for(int peer=0;peer<room.capacity;++peer)if(peer!=source && !room.players[peer].empty() && room.entered[peer]) {
            // Legacy viewers keep receiving the original full protocol.
            if(!room.boardViews[peer]) {budget.full=true;continue;}
            for(const auto& view:room.boardViews[peer]->players)if(view.slot==source) {
                if(view.full)budget.full=true;
                else {budget.columns=std::max(budget.columns,view.columns);budget.rows=std::max(budget.rows,view.rows);budget.fps=std::max(budget.fps,view.fps);}
            }
        }
        budget.highRate=room.boardViews[source] && room.boardViews[source]->highRate;
        if(!budget.highRate)budget.fps=std::min(budget.fps,12);
        const auto wire=encode_board_budget(budget);
        if(source==resendSlot || room.boardBudgets[source]!=wire) {
            room.boardBudgets[source]=wire;send_wire(room,source,encode_match_packet(room.matchEpoch,wire));
        }
    }
}

void RelayServer::flush_mini_boards(Room& room,Clock::time_point now) {
    if(room.phase!=Phase::Playing)return;
    for(int peer=0;peer<room.capacity;++peer)if(room.entered[peer] && room.boardViews[peer]) {
        std::vector<const BoardView*> mini;int fps=1;
        for(const auto& view:room.boardViews[peer]->players)if(!view.full && view.slot!=peer && !room.players[view.slot].empty()) {
            mini.push_back(&view);fps=std::max(fps,view.fps);
        }
        if(mini.empty())continue;
        const std::size_t batches=(mini.size()+2)/3;
        const auto interval=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1./(fps*batches)));
        if(now-room.lastMiniBatch[peer]<interval)continue;
        if(room.lastMiniBatch[peer]==Clock::time_point{} || now-room.lastMiniBatch[peer]>=interval*2)room.lastMiniBatch[peer]=now;
        else room.lastMiniBatch[peer]+=interval;
        const std::size_t start=room.miniBatchCursor[peer]%mini.size(),count=std::min<std::size_t>(3,mini.size()-start);
        room.miniBatchCursor[peer]=(start+count)%mini.size();
        std::vector<std::string> events;events.reserve(count);
        for(std::size_t i=0;i<count;++i) {
            const auto& view=*mini[(start+i)%mini.size()];const auto& cached=room.miniBoards[view.slot];if(!cached)continue;
            const auto last=room.lastMiniForward[view.slot][peer];
            const auto frameInterval=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1./view.fps));
            if(now-last<frameInterval)continue;
            if(room.miniForwardSequence[view.slot][peer]==cached->sequence && now-last<std::chrono::seconds(1))continue;
            room.lastMiniForward[view.slot][peer]=last==Clock::time_point{} || now-last>=frameInterval*2?now:last+frameInterval;
            room.miniForwardSequence[view.slot][peer]=cached->sequence;
            auto& projections=room.miniProjections[view.slot];
            const auto dimensions=std::make_pair(view.columns,view.rows);
            auto projection=projections.find(dimensions);
            if(projection==projections.end()) {
                if(projections.size()>=4)projections.erase(projections.begin());
                projection=projections.emplace(dimensions,project_mini_board(*cached,view.columns,view.rows)).first;
            }
            auto projected=projection->second;
            projected.sequence=cached->sequence;projected.clock=cached->clock;
            if(projected.clock && projected.clock->rate>0) {
                // Repair heartbeats may reuse an unchanged board. Advance
                // its wall clock through time spent in the relay cache so
                // receiving that heartbeat never restarts an old sample.
                const double age=std::max(0.,std::chrono::duration<double>(now-room.miniBoardReceived[view.slot]).count());
                projected.clock->elapsedSeconds=std::min(1e18,projected.clock->elapsedSeconds+age*projected.clock->rate);
            }
            events.push_back(encode_player_packet({room.matchEpoch,view.slot,encode_mini_board(projected)}));
        }
        send_mini_batch(room,peer,events);
    }
}

void RelayServer::publish_multiplayer_phase(Room& room) {
    if(room.phase==Phase::Waiting) return;
    // Setup changes already broadcast immediately. Keep UDP recovery, without
    // resending the whole N-by-N roster at the 25ms config-chunk cadence.
    const auto now=Clock::now();
    const bool resendSetups=now-room.lastSetupPublish>=std::chrono::milliseconds(250);
    if(resendSetups)room.lastSetupPublish=now;
    // Ready/acknowledgement traffic can publish the phase many times per RTT.
    // Pace configuration independently so an ack never multiplies every
    // other recipient's outstanding chunks into another upload burst.
    const bool resendConfig=now-room.lastConfigPublish>=std::chrono::milliseconds(25);
    if(resendConfig)room.lastConfigPublish=now;
    if(room.phase==Phase::Playing) select_targets(room);
    const int host=static_cast<int>(std::find(room.players.begin(),room.players.end(),room.host)-room.players.begin());
    for(int slot=0;slot<room.capacity;++slot) {
        const auto client=clients_.find(room.players[slot]);if(client==clients_.end()) continue;
        if(room.phase==Phase::RoundAck) {
            send_wire(room,slot,"!match-finished;"+std::to_string(room.matchEpoch)+";"+std::to_string(room.winner));
            continue;
        }
        if(resendConfig && room.policy.forceRules &&
            ((slot==host && room.phase==Phase::Preparing && !room.manualPreparation) || room.entered[slot]) && !room.configApplied[slot]) {
            ConfigMessage begin;begin.token=client->second.token;begin.epoch=room.configEpoch;
            begin.roomInstance=room.instance;begin.hostSlot=host;begin.policy=room.policy;
            send_wire(room,slot,encode_config_message(begin));
            if(room.config.payload) {
                const auto count=room.config.parts.size();
                if(room.configReceived[slot].size()!=count) room.configReceived[slot].assign(count,false);
                std::size_t scanned=0,sent=0;
                while(scanned++<count && sent<32) {
                    const auto index=room.configCursor[slot]++%count;
                    if(!room.configReceived[slot][index]) {
                        send_wire(room,slot,encode_config_message(room_config_part(*room.config.payload,client->second.token,
                            room.configEpoch,room.config.digest,index)));++sent;
                    }
                }
            }
        }
        if(!room.entered[slot] && !room.participating[slot]) continue;
        if(!room.policy.forceRules || room.configApplied[slot]) {
          if(room.phase==Phase::Preparing || !room.active[slot]) {
            send_wire(room,slot,encode_match_packet(room.matchEpoch,encode_server_lobby_signal(ServerLobbySignal::ConfigExchangeReady)));
            send_wire(room,slot,encode_match_packet(room.matchEpoch,room.seed));
          }
        } else continue;
        for(int peer=0;peer<room.capacity;++peer) if(resendSetups && !room.active[slot] && peer!=slot && !room.setup[peer].empty())
            send_wire(room,slot,encode_player_packet({room.matchEpoch,peer,room.setup[peer]}));
        if(room.phase==Phase::Countdown) {
            if(room.scheduledCountdown) {
                const auto remaining=std::chrono::duration_cast<std::chrono::microseconds>(
                    room.phaseStarted+std::chrono::seconds(3)-Clock::now()).count();
                send_wire(room,slot,encode_match_packet(room.matchEpoch,encode_countdown_clock({0,
                    static_cast<int>(std::clamp<std::int64_t>(remaining,0,3000000))})));
            }
            send_wire(room,slot,encode_match_packet(room.matchEpoch,encode_server_lobby_signal(ServerLobbySignal::CountdownReady)));
        }
        if(room.phase==Phase::Playing && !room.active[slot]) {
            if(room.scheduledCountdown)
                send_wire(room,slot,encode_match_packet(room.matchEpoch,encode_countdown_clock({0,0})));
            send_wire(room,slot,encode_match_packet(room.matchEpoch,encode_server_lobby_signal(ServerLobbySignal::RoundReady)));
            for(int peer=0;peer<room.capacity;++peer) if(peer!=slot && !room.snapshots[peer].empty())
                send_wire(room,slot,encode_player_packet({room.matchEpoch,peer,room.snapshots[peer]}));
        }
        send_wire(room,slot,"!attack-delivered;"+std::to_string(room.matchEpoch)+";"+std::to_string(room.attackAckCursor[slot]));
        int burst=0;
        if(room.phase==Phase::Playing) for(const auto& delivery:room.pendingAttacks[slot]) {
            if(++burst>32) break;
            const auto wire=encode_attack_frame({Runtime::MultiplayerSequenceId::from_wire(delivery.sequence),
                Runtime::MultiplayerSequenceId::from_wire(room.attackAckCursor[slot]),delivery.amount});
            send_wire(room,slot,encode_player_packet({room.matchEpoch,delivery.source,wire}));
        }
    }
    room.lastPublish=Clock::now();
}

void RelayServer::advance_scheduled_countdown(Room& room,const std::string& name) {
    const auto now=Clock::now();
    if(!room.multiplayer || room.phase!=Phase::Countdown || !room.scheduledCountdown ||
       now<room.phaseStarted+std::chrono::seconds(3)) return;
    const auto started=room.phaseStarted+std::chrono::seconds(3);
    room.phase=Phase::Playing;room.active={};room.lastPublish={};room.phaseStarted=now;
    start_room_replay(room,started);
    publish_multiplayer_phase(room);publish_room(name,room);
}

void RelayServer::handle_multiplayer_gameplay(Room& room,Client& client,int slot,std::string_view incoming) {
    const auto envelope=decode_match_packet(incoming);
    if(!envelope) return;
    const bool current=envelope->first==room.matchEpoch;
    const bool previous=room.previousResults && envelope->first==room.previousResults->epoch;
    if(!current && !previous)return;
    const auto& payload=envelope->second;
    if(payload.rfind("!result-part-v1;",0)==0) {
        const bool actor=current ? room.participating[slot] && (room.phase==Phase::Playing || room.phase==Phase::RoundAck) :
            room.previousResults->actors[slot]==sender_key(client.address);
        if(!actor)return;
        auto& cached=current?room.results[slot]:room.previousResults->results[slot];
        auto& upload=current?room.resultUploads[slot]:room.previousResults->uploads[slot];
        const auto result=upload.insert(payload);if(!result)return;
        const bool changed=cached!=*result;cached=*result;
        send_wire(room,slot,"!result-stored;"+std::to_string(envelope->first));
        if(changed) for(int peer=0;peer<room.capacity;++peer) if(!room.players[peer].empty())
            for(const auto& part:round_statistics_parts(cached))
                send_wire(room,peer,encode_player_packet({envelope->first,slot,part}));
        return;
    }
    if(payload.rfind("!results-request;",0)==0) {
        const auto f=Util::Text::split_exact<3>(payload,';');if(!f)return;
        const auto actor=Util::Text::parse_integer<int>((*f)[1]);const auto first=Util::Text::parse_integer<std::size_t>((*f)[2]);
        if(!actor||*actor<0||*actor>=room.capacity||!first)return;
        const auto& cached=current?room.results[*actor]:room.previousResults->results[*actor];if(cached.empty())return;
        const auto parts=round_statistics_parts(cached);
        for(std::size_t i=*first;i<parts.size() && i-*first<8;++i)
            send_wire(room,slot,encode_player_packet({envelope->first,*actor,parts[i]}));
        return;
    }
    if(!current)return;
    // receive_for() can sleep across the deadline. Recheck before deciding
    // whether the very first gameplay packet belongs to Countdown or Playing.
    advance_scheduled_countdown(room,client.room);
    const std::string& wire=envelope->second;
    if(decode_field_visibility(wire)) {
        if(room.entered[slot])for(int peer=0;peer<room.capacity;++peer)
            if(peer!=slot && room.entered[peer])send_wire(room,peer,encode_player_packet({room.matchEpoch,slot,wire}));
        return;
    }
    if(wire.rfind("!view-v1;",0)==0 || wire.rfind("!view-v2;",0)==0) {
        const auto views=decode_board_views(wire);if(!views || !room.entered[slot])return;
        const bool changed=!room.boardViews[slot] || encode_board_views(*room.boardViews[slot])!=wire;
        if(changed){room.lastMiniBatch[slot]={};room.miniBatchCursor[slot]=0;}
        for(const auto& view:views->players)if(changed && view.slot!=slot && !room.players[view.slot].empty()) {
            const BoardView* previous=nullptr;
            if(room.boardViews[slot])for(const auto& old:room.boardViews[slot]->players)if(old.slot==view.slot){previous=&old;break;}
            if(view.full && (!previous || !previous->full) && !room.snapshots[view.slot].empty())
                send_wire(room,slot,encode_player_packet({room.matchEpoch,view.slot,room.snapshots[view.slot]}));
            else if(!view.full && (!previous || previous->full || previous->columns!=view.columns || previous->rows!=view.rows || previous->fps!=view.fps)) {
                room.lastMiniForward[view.slot][slot]={};room.miniForwardSequence[view.slot][slot]=0;
            }
        }
        room.boardViews[slot]=*views;
        // Each viewer heartbeat refreshes its own sender budget. Resending all
        // N budgets on each of N heartbeats would create N-squared controls.
        update_board_budgets(room,slot);return;
    }
    const auto signal=decode_client_lobby_signal(wire);
    if(const auto clock=decode_countdown_clock(wire);clock && !clock->remainingMicros) {
        room.countdownClockCapable[slot]=true;
        int remaining=-1;
        if(room.phase==Phase::Countdown && room.scheduledCountdown)
            remaining=static_cast<int>(std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                room.phaseStarted+std::chrono::seconds(3)-Clock::now()).count(),0,3000000));
        else if(room.phase==Phase::Playing && room.scheduledCountdown) remaining=0;
        send_wire(room,slot,encode_match_packet(room.matchEpoch,encode_countdown_clock({clock->probe,remaining})));
        return;
    }
    if(signal && *signal==ClientLobbySignal::ReadyForConfig) {
        room.entered[slot]=true;
        update_board_budgets(room);
        if(!room.participating[slot] || ((room.phase==Phase::Playing || room.phase==Phase::Countdown) && !room.alive[slot]))
            room.active[slot]=false;
        publish_multiplayer_phase(room);return;
    }
    if(signal && *signal==ClientLobbySignal::Cancel) {
        if(room.manualPreparation) {
            room.ready[slot]=false;room.entered[slot]=false;room.active[slot]=false;room.configApplied[slot]=false;
            room.boardViews[slot].reset();room.startRequested=false;
            if(room.phase==Phase::Preparing && room.host==sender_key(client.address))restart_multiplayer_preparation(room);
            if(room.phase==Phase::Playing || room.phase==Phase::Countdown)eliminate_player(room,slot);
            update_board_budgets(room);publish_room(client.room,room);return;
        }
        // An observer's UI lifecycle never changes this round's roster,
        // readiness, countdown or epoch, including when the observer is host.
        if(!room.participating[slot] || ((room.phase==Phase::Playing || room.phase==Phase::Countdown) && !room.alive[slot])) {
            room.entered[slot]=false;room.active[slot]=false;room.configApplied[slot]=false;room.boardViews[slot].reset();update_board_budgets(room);return;
        }
        if(room.host==sender_key(client.address) && (room.phase==Phase::Playing || room.phase==Phase::Countdown)) {
            begin_multiplayer_match(room);publish_multiplayer_phase(room);publish_room(client.room,room);return;
        }
        room.ready[slot]=false;room.entered[slot]=false;room.active[slot]=false;room.configApplied[slot]=false;
        if(room.phase==Phase::Playing || room.phase==Phase::Countdown) eliminate_player(room,slot);
        else start_ready_players(room,false);
        publish_room(client.room,room);return;
    }
    if(auto setup=decode_lobby_player_setup(wire);setup && room.phase==Phase::Preparing && room.entered[slot] &&
            (!room.policy.forceRules || room.configApplied[slot])) {
            setup->playerIndex=-1;room.setup[slot]=encode_lobby_player_setup(*setup);
            for(int peer=0;peer<room.capacity;++peer) if(peer!=slot && room.entered[peer])
                send_wire(room,peer,encode_player_packet({room.matchEpoch,slot,room.setup[slot]}));
        return;
    }
    if(room.phase==Phase::Preparing) return;
    if(room.phase==Phase::Countdown && signal && *signal==ClientLobbySignal::CountdownComplete) {
        if(room.scheduledCountdown) return; // A slow or missing client cannot extend the deadline.
        if(room.participating[slot]) room.active[slot]=true;
        bool all=true;
        for(int peer=0;peer<room.capacity;++peer) if(room.alive[peer] && !room.active[peer]) all=false;
        if(all) {room.phase=Phase::Playing;room.active={};room.phaseStarted=Clock::now();start_room_replay(room);publish_multiplayer_phase(room);publish_room(client.room,room);}
        return;
    }
    if(room.phase!=Phase::Playing) return;
    if(wire.rfind("!attack-ack;",0)==0) {
        const auto ack=Util::Text::parse_integer<int>(std::string_view(wire).substr(12));
        if(!ack || *ack<0 || *ack>room.receivedAttackId[slot]) return;
        auto& pending=room.pendingAttacks[slot];
        while(!pending.empty() && pending.front().sequence<=*ack) {
            const auto delivery=pending.front();pending.pop_front();acknowledge_delivery(room,delivery.source,delivery.sourceSequence);
        }
        return;
    }
    if(signal || decode_lobby_player_setup(wire)) return;
    if(wire.rfind("!loss-v1;",0)==0) {
        const auto source=Util::Text::parse_integer<int>(std::string_view(wire).substr(9));
        if(source && (*source==-1 || room.attackSources[slot].count(*source))) {room.lastAttacker[slot]=*source;eliminate_player(room,slot);}
        return;
    }
    if(const auto loss=decode_round_signal(wire)) {
        if(*loss==OutgoingRoundSignal::LeaveRound) room.lastAttacker[slot]=-1;
        eliminate_player(room,slot);return;
    }
    if(wire.rfind("!snapshot-request;",0)==0) {
        room.active[slot]=true;
        for(int peer=0;peer<room.capacity;++peer) if(peer!=slot && room.participating[peer]) {
            if(room.boardViews[slot] && std::none_of(room.boardViews[slot]->players.begin(),room.boardViews[slot]->players.end(),
                [&](const BoardView& view){return view.slot==peer && view.full;}))continue;
            if(!room.snapshots[peer].empty()) send_wire(room,slot,encode_player_packet({room.matchEpoch,peer,room.snapshots[peer]}));
            send_wire(room,peer,encode_match_packet(room.matchEpoch,wire));
        }
        return;
    }
    if(!room.alive[slot]) return;
    room.active[slot]=true;
    if(wire.rfind("!mini-v1;",0)==0) {
        auto frame=decode_mini_board(wire);if(!frame)return;
        if(room.miniBoards[slot] && frame->sequence<room.miniBoards[slot]->sequence)return;
        const auto& previous=room.miniBoards[slot];
        const bool changed=!previous || frame->columns!=previous->columns || frame->rows!=previous->rows ||
            frame->logicalWidth!=previous->logicalWidth || frame->logicalHeight!=previous->logicalHeight || frame->cells!=previous->cells;
        if(previous && frame->sequence==previous->sequence && changed)return;
        if(changed)room.miniProjections[slot].clear();
        // Keep only the newest state. Delivery is paced per viewer in poll(),
        // never a burst of every actor's board on one gameplay packet.
        room.miniBoards[slot]=std::move(*frame);
        room.miniBoardReceived[slot]=Clock::now();
        return;
    }
    const auto packet=decode_frame_packet(wire,10,20);
    if(packet && packet->type==FramePacketType::Attack) {
        const int sequence=packet->receiveId.value();
        if(sequence<=0 || sequence>room.attackAckCursor[slot]+4096 || packet->attack>1000000) return;
        const int target=room.target[slot];
        if(sequence>room.attackAckCursor[slot] && !room.seenAttacks[slot].count(sequence) && target>=0 && room.alive[target] &&
            (room.pendingAttacks[target].size()>=4096 || room.receivedAttackId[target]==std::numeric_limits<int>::max())) return;
        const auto prior=room.attackTargets[slot].find(sequence);
        const int actual=prior==room.attackTargets[slot].end()?target:prior->second;
        send_wire(room,slot,"!attack-stored;"+std::to_string(room.matchEpoch)+";"+std::to_string(sequence)+";"+std::to_string(actual));
        if(sequence<=room.attackAckCursor[slot] || !room.seenAttacks[slot].insert(sequence).second) return;
        room.attackTargets[slot][sequence]=target;
        if(!packet->attack || target<0 || !room.alive[target]) {acknowledge_delivery(room,slot,sequence);return;}
        if(room.pendingAttacks[target].size()>=4096 || room.receivedAttackId[target]==std::numeric_limits<int>::max()) return;
        const int delivery=++room.receivedAttackId[target];room.lastAttacker[target]=slot;
        room.attackSources[target].insert(slot);
        room.pendingAttacks[target].push_back({slot,sequence,delivery,packet->attack});
        send_wire(room,target,encode_player_packet({room.matchEpoch,slot,encode_attack_frame({
            Runtime::MultiplayerSequenceId::from_wire(delivery),Runtime::MultiplayerSequenceId::from_wire(room.attackAckCursor[target]),packet->attack})}));
        return;
    }
    if(wire.size()>65000 || !is_gameplay_extension(wire)) return;
    if(wire.rfind("!snapshot-v1;",0)==0 || wire.rfind("!board-v1;",0)==0 || (wire.size()>1 && wire[0]=='!' &&
        (wire[1]=='-' || (wire[1]>='0' && wire[1]<='9') || (wire[1]>='A' && wire[1]<='Z')))) room.snapshots[slot]=wire;
    const bool visual=wire.rfind("!motion-v1;",0)==0 || wire.rfind("!snapshot-v1;",0)==0 || wire.rfind("!board-v1;",0)==0;
    for(int peer=0;peer<room.capacity;++peer) if(peer!=slot && !room.players[peer].empty()) {
        if(visual && !room.entered[peer])continue;
        if(visual && room.boardViews[peer] && std::none_of(room.boardViews[peer]->players.begin(),room.boardViews[peer]->players.end(),
            [&](const BoardView& view){return view.slot==slot && (view.full || !room.boardViews[slot]);}))continue;
        send_wire(room,peer,encode_player_packet({room.matchEpoch,slot,wire}));
    }
}
} // namespace Network
