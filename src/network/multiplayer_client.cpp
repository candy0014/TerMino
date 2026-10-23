#include "tetr/network/network_services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/transport.h"
#include "tetr/runtime/game_context.h"
#include "tetr/util/text_codec.h"
#include "tetr/network/round_statistics.h"
#include <algorithm>

namespace Network {
namespace {
bool board_visual(std::string_view wire) {
    if(wire.rfind("!motion-v1;",0)==0 || wire.rfind("!snapshot-v1;",0)==0 || wire.rfind("!board-v1;",0)==0)return true;
    return wire.size()>1 && wire[0]=='!' && (wire[1]=='-' || (wire[1]>='0' && wire[1]<='9') || (wire[1]>='A' && wire[1]<='Z'));
}
bool wants_full_board(const NetworkServices& n,int slot) {
    if(n.lastBoardViews.empty())return true;
    for(const auto& view:n.boardViews.players)if(view.slot==slot)
        return view.full || !n.peerMiniBoards.count(slot); // Legacy senders project locally.
    return false;
}
}
void synchronize_room_player_mode(NetworkServices& n,bool online,std::optional<int> gameMode) {
    if(!n.initialized) return;
    if(!n.multiplayerRoom || n.room.name.empty()) {
        n.roomOnlineMode.reset();n.roomGameMode.reset();n.roleRoomInstance=0;return;
    }
    const bool joined=n.roleRoomInstance!=n.roomInstance;
    const bool changed=(n.roomOnlineMode && *n.roomOnlineMode!=online) ||
        (gameMode && n.roomGameMode && *gameMode!=*n.roomGameMode);
    const auto self=std::find_if(n.roomMembers.begin(),n.roomMembers.end(),[&](const auto& m){return m.slot==n.roomPlayerIndex;});
    if(self==n.roomMembers.end()) return;
    if(joined || changed) {
        const auto desired=!online?PlayerRole::Offline:self->role==PlayerRole::Offline?PlayerRole::Battle:self->role;
        if(desired!=self->role) {
            if(n.pendingRoomRequest) return;
            request_room(n,RoomOperation::SetRole,{},true,false,static_cast<int>(desired));
        }
    }
    n.roomOnlineMode=online;n.roleRoomInstance=n.roomInstance;
    if(gameMode)n.roomGameMode=gameMode;
}
bool handle_room_game_control(Runtime::GameContext& context,bool retry) {
    if(!context.network_component())return true;
    auto& n=network_services(context);
    if(!n.multiplayerRoom || n.connection!=ConnectionState::Connected)return true;
    auto& scene=context.session.roomScene;
    if(scene.enabled && scene.epoch==n.matchEpoch && !scene.spectator && !scene.localForfeit && scene.selfAlive &&
       (n.matchPhase==RoomMatchPhase::Playing || n.matchPhase==RoomMatchPhase::Countdown)) {
        auto& player=context.session.player;
        player.presentation.activePose.reset();player.presentation.activePieceType.reset();
        send_board_state_frame(context,player,ActivePieceState::locked(),n.boardSync.lastPlacement,true);
        send_round_signal(context,OutgoingRoundSignal::LeaveRound);
        scene.localForfeit=true;scene.selfAlive=false;
    }
    if(!retry) {
        send_lobby_signal(context,ClientLobbySignal::Cancel);
    }
    return true;
}
void reset_multiplayer_state(NetworkServices& n) {
    n.matchPhase=RoomMatchPhase::Idle;n.matchEpoch=0;n.matchCanceled=false;n.winnerSlot=n.targetSlot=-1;
    n.matchNoticeUntil={};n.lastNoticePaint={};
    n.countdownClockAvailable=false;n.countdownLegacy=false;n.countdownStarted=false;n.countdownDeadline.reset();n.countdownProbes.clear();
    n.countdownBestRtt=std::chrono::seconds(15);n.lastCountdownProbe={};
    n.deliveredAttack=n.receivedAttack=0;n.playerSetups.clear();n.playerInbox.clear();n.peerTimelines.clear();
    n.incomingAttacks.clear();n.outgoingAttacks.clear();n.actualAttackTargets.clear();n.localPlayerSetup.clear();n.lastAttackSend={};n.peerSnapshots.clear();n.peerTurns.clear();
    n.peerMiniBoards.clear();n.peerFieldVisibility.clear();n.lastFieldVisibility.clear();n.lastFieldVisibilitySend={};
    n.boardBudget.reset();n.boardViews={};n.lastBoardViews.clear();n.lastBoardViewSend={};n.lastMiniSend={};n.lastMiniSample={};n.lastMiniFrame.reset();
    n.forceFullCheckpoint=false;n.boardViewsV2Confirmed=false;n.miniUpdateBudget=240;
    n.visualRtts={};n.visualRttSamples=0;
    n.lastVisualRateChange={};n.visualGoodSince={};n.miniSequence=n.miniFramesSent=n.miniBytesSent=0;
}
void apply_multiplayer_state(NetworkServices& n,const RoomResponse& r) {
    if(n.resultRoomInstance!=r.roomInstance) {
        n.roundResultWires.clear();n.roundResultNames.clear();n.roundParticipants.clear();n.resultIncoming.clear();
        n.pendingResultEpoch.reset();n.pendingRoundResult.clear();n.resultRoomInstance=r.roomInstance;
    }
    if(r.multiplayer && r.matchEpoch && r.matchPhase!=RoomMatchPhase::Preparing && r.matchPhase!=RoomMatchPhase::Idle) {
        for(const auto& m:r.members)n.roundResultNames[r.matchEpoch][m.slot]=m.username+
            (r.matchPhase==RoomMatchPhase::Finished && m.participating?(m.alive?" WIN":" LOSE"):"");
        if(r.matchPhase==RoomMatchPhase::Countdown || r.matchPhase==RoomMatchPhase::Playing || r.matchPhase==RoomMatchPhase::Finished) {
            auto& actors=n.roundParticipants[r.matchEpoch];actors.clear();for(const auto& m:r.members)if(m.participating)actors.push_back(m.slot);
        }
        const auto trim=[](auto& cache){while(cache.size()>2)cache.erase(cache.begin());};
        trim(n.roundResultNames);trim(n.roundParticipants);trim(n.roundResultWires);trim(n.resultIncoming);
    }
    if(n.matchEpoch!=r.matchEpoch || n.multiplayerRoom!=r.multiplayer) {
        reset_multiplayer_state(n);n.onlineInbox.clear();n.roomConfig={};n.lastAppliedConfigEpoch=0;
    }
    const auto oldPhase=n.matchPhase;
    n.multiplayerRoom=r.multiplayer;n.matchEpoch=r.matchEpoch;n.matchPhase=r.matchPhase;n.battleLimit=r.battleLimit;
    if(n.countdownStarted && r.matchPhase==RoomMatchPhase::Countdown) n.matchPhase=RoomMatchPhase::Playing;
    if(n.multiplayerRoom && r.matchEpoch && r.matchPhase!=RoomMatchPhase::Idle && n.matchNoticeEpoch!=r.matchEpoch) {
        n.matchNoticeEpoch=r.matchEpoch;n.matchNoticeUntil=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    }
    if(oldPhase!=r.matchPhase) ++n.roomRevision;
    for(const auto& m:r.members) if(m.slot==n.roomPlayerIndex) n.targetSlot=m.target;
    if(r.matchPhase==RoomMatchPhase::Finished) {
        n.winnerSlot=-1;for(const auto& m:r.members) if(m.participating && m.alive) n.winnerSlot=m.slot;
    }
    if(oldPhase!=RoomMatchPhase::Idle && r.matchPhase==RoomMatchPhase::Idle) {n.matchCanceled=true;n.matchNoticeUntil={};n.onlineInbox.clear();n.playerInbox.clear();n.roomConfig={};}
}
bool handle_multiplayer_message(NetworkServices& n,std::string_view wire) {
    if(!n.multiplayerRoom) return false;
    if(wire.rfind("!result-stored;",0)==0) {
        const auto epoch=Util::Text::parse_integer<std::uint64_t>(wire.substr(15));
        if(epoch && n.pendingResultEpoch==epoch){n.pendingResultEpoch.reset();n.pendingRoundResult.clear();}return true;
    }
    if(const auto message=decode_match_packet(wire)) {
        if(message->first!=n.matchEpoch) return true;
        if(auto budget=decode_board_budget(message->second)) {
            if(budget->highRate)n.boardViewsV2Confirmed=true;
            if(budget->full && (!n.boardBudget || !n.boardBudget->full))n.forceFullCheckpoint=true;
            if(!n.boardBudget || n.boardBudget->columns!=budget->columns || n.boardBudget->rows!=budget->rows || n.boardBudget->fps!=budget->fps)n.lastMiniSample={};
            n.boardBudget=*budget;return true;
        }
        if(const auto clock=decode_countdown_clock(message->second);clock && clock->remainingMicros) {
            const auto now=std::chrono::steady_clock::now();
            auto rtt=n.countdownBestRtt;
            bool measured=false;
            if(clock->probe) {
                const auto probe=n.countdownProbes.find(clock->probe);
                if(probe==n.countdownProbes.end()) return true;
                rtt=now-probe->second;measured=true;n.countdownProbes.erase(probe);
            }
            n.countdownClockAvailable=true;
            if(*clock->remainingMicros==-1 && n.matchPhase==RoomMatchPhase::Countdown) n.countdownLegacy=true;
            const bool better=rtt<=n.countdownBestRtt;
            if(measured && better) n.countdownBestRtt=rtt;
            if(n.matchPhase==RoomMatchPhase::Playing &&
               (!n.countdownClockRequested || n.countdownStarted || n.boardViewsV2Confirmed)) {
                // Reuse the clock echo as a live queue-delay measurement;
                // it must not modify an already running start deadline.
                if(measured && n.boardViewsV2Confirmed) {
                    using Duration=std::chrono::steady_clock::duration;
                    n.visualRtts[n.visualRttSamples++%n.visualRtts.size()]=rtt;
                    const auto count=std::min(n.visualRttSamples,n.visualRtts.size());
                    // An isolated scheduling/route spike is not sustained
                    // congestion. Keep a short delay window before cutting
                    // the rate; otherwise one noisy echo can leave many
                    // players at the minimum rate for the rest of the game.
                    auto sorted=n.visualRtts;std::sort(sorted.begin(),sorted.begin()+count);
                    const auto extra=sorted[count/2]-n.countdownBestRtt;
                    const bool severe=rtt>n.countdownBestRtt*2 && rtt-n.countdownBestRtt>std::chrono::milliseconds(150);
                    if(severe || (count==n.visualRtts.size() && extra>std::max<Duration>(std::chrono::milliseconds(25),n.countdownBestRtt/4))) {
                        n.visualGoodSince={};
                        if(now-n.lastVisualRateChange>=std::chrono::milliseconds(500)) {
                            int next=n.miniUpdateBudget*3/4;
                            // A very late echo is already evidence of a long
                            // queue. Do not wait for two more late probes or
                            // drain it with several small quarter-rate cuts.
                            if(severe)next=std::min(next,int(n.miniUpdateBudget*(2.*std::chrono::duration<double>(n.countdownBestRtt).count()/std::chrono::duration<double>(rtt).count())));
                            n.miniUpdateBudget=std::max(60,next);n.lastVisualRateChange=now;
                        }
                    } else if(extra<=std::max<Duration>(std::chrono::milliseconds(15),n.countdownBestRtt/10)) {
                        if(n.visualGoodSince==std::chrono::steady_clock::time_point{})n.visualGoodSince=now;
                        if(now-n.visualGoodSince>=std::chrono::seconds(2) && now-n.lastVisualRateChange>=std::chrono::seconds(1)) {
                            n.miniUpdateBudget=std::min(480,n.miniUpdateBudget+30);n.lastVisualRateChange=now;
                        }
                    } else n.visualGoodSince={};
                }
                return true;
            }
            if(!n.countdownStarted && *clock->remainingMicros>=0 && (n.matchPhase==RoomMatchPhase::Countdown || n.matchPhase==RoomMatchPhase::Playing) &&
               (!n.countdownDeadline || (measured && better))) {
                // Map the server's relative deadline onto our steady clock.
                // The shortest RTT avoids treating queued packets as fresh time.
                const auto transit=rtt==std::chrono::seconds(15) ? std::chrono::steady_clock::duration{} : rtt/2;
                n.countdownDeadline=now+std::chrono::microseconds(*clock->remainingMicros)-transit;
            }
            return true;
        }
        if(message->first==n.matchEpoch && n.matchPhase!=RoomMatchPhase::Idle &&
            (decode_server_lobby_signal(message->second) || decode_lobby_seed(message->second) || decode_snapshot_request(message->second))) {
            if(message->second==encode_server_lobby_signal(ServerLobbySignal::RoundReady) && n.matchPhase==RoomMatchPhase::Countdown) {
                n.matchPhase=RoomMatchPhase::Playing;++n.roomRevision;
            }
            if(message->second==encode_server_lobby_signal(ServerLobbySignal::ConfigExchangeReady) && n.roomConfig.applied) n.roomConfig.finished=true;
            if(n.onlineInbox.size()<1024 && std::find(n.onlineInbox.begin(),n.onlineInbox.end(),message->second)==n.onlineInbox.end()) n.onlineInbox.push_back(message->second);
        }
        return true;
    }
    if(auto routed=decode_player_packet(wire)) {
        if(routed->data.rfind("!result-part-v1;",0)==0) {
            if(routed->epoch!=n.matchEpoch && !n.roundParticipants.count(routed->epoch))return true;
            auto& upload=n.resultIncoming[routed->epoch][routed->slot];if(!upload)upload=std::make_shared<ResultAssembler>();
            if(auto result=upload->insert(routed->data))n.roundResultWires[routed->epoch][routed->slot]=std::move(*result);
            return true;
        }
        if(routed->epoch!=n.matchEpoch || routed->slot==n.roomPlayerIndex) return true;
        if(const auto visibility=decode_field_visibility(routed->data)) {
            n.peerFieldVisibility[routed->slot]=*visibility;return true;
        }
        if(routed->data.rfind("!mini-v1;",0)==0) {
            // A resize/focus change can leave old visual packets in flight.
            // Drop those before decoding once the viewer has unsubscribed.
            if(!n.lastBoardViews.empty() && std::none_of(n.boardViews.players.begin(),n.boardViews.players.end(),
                [&](const BoardView& view){return view.slot==routed->slot && !view.full;}))return true;
            if(auto frame=decode_mini_board(routed->data)) {
                auto found=n.peerMiniBoards.find(routed->slot);
                if(found==n.peerMiniBoards.end() || frame->sequence>=found->second.sequence) {
                    frame->receivedRevision=found==n.peerMiniBoards.end()?1:found->second.receivedRevision+1;
                    auto& setup=n.playerSetups[routed->slot];setup.width=frame->logicalWidth;setup.visibleHeight=frame->logicalHeight;
                    n.peerMiniBoards[routed->slot]=std::move(*frame);
                    if(!n.lastBoardViews.empty())n.peerTimelines.erase(routed->slot);
                }
            }
            return true;
        }
        if(auto setup=decode_lobby_player_setup(routed->data)) {n.playerSetups[routed->slot]=*setup;return true;}
        if(board_visual(routed->data) && !wants_full_board(n,routed->slot))return true;
        if(routed->data.rfind("!board-v1;",0)==0) {
            const auto a=routed->data.find(';',10),b=a==std::string::npos?a:routed->data.find(';',a+1),d=b==std::string::npos?b:routed->data.find(';',b+1);
            if(a==std::string::npos || b==std::string::npos || d==std::string::npos) return true;
            const auto view=std::string_view(routed->data);
            const auto width=Util::Text::parse_integer<int>(view.substr(10,a-10)),height=Util::Text::parse_integer<int>(view.substr(a+1,b-a-1));
            const auto next=Util::Text::parse_integer<int>(view.substr(b+1,d-b-1));
            if(!width || *width<1 || *width>100 || !height || *height<0 || *height>100 || !next || *next<0 || *next>100) return true;
            auto& setup=n.playerSetups[routed->slot];setup.width=*width;setup.visibleHeight=*height;setup.nextCount=*next;
            routed->data=routed->data.substr(d+1);
        }
        if(n.playerInbox.size()<8192) n.playerInbox.push_back(std::move(*routed));
        return true;
    }
    const auto f=Util::Text::split(wire,';');
    if(f.size()<2) return false;
    const bool recognized=f[0]=="!match-canceled" || f[0]=="!match-finished" || f[0]=="!knockout" ||
        f[0]=="!target" || f[0]=="!attack-stored" || f[0]=="!attack-delivered";
    if(!recognized) return false;
    const auto epoch=Util::Text::parse_integer<std::uint64_t>(f[1]);
    if(!epoch || *epoch!=n.matchEpoch) return true;
    if(f[0]=="!match-canceled" && f.size()==2) {
        n.matchCanceled=true;n.matchNoticeUntil={};n.matchPhase=RoomMatchPhase::Idle;n.onlineInbox.clear();n.playerInbox.clear();n.roomConfig={};++n.roomRevision;return true;
    }
    if(f.size()<3) return true;
    const auto value=Util::Text::parse_integer<int>(f[2]);if(!value) return true;
    if(f[0]=="!attack-stored") {
        n.outgoingAttacks.erase(*value);
        if(f.size()==4) {const auto target=Util::Text::parse_integer<int>(f[3]);if(target && *target>=-1 && *target<kMultiplayerRoomCapacity) n.actualAttackTargets[*value]=*target;}
    }
    else if(f[0]=="!attack-delivered") n.deliveredAttack=std::max(n.deliveredAttack,*value);
    else if(f[0]=="!target" && *value>=-1 && *value<kMultiplayerRoomCapacity) n.targetSlot=*value;
    else if(f[0]=="!match-finished" && *value>=-1 && *value<kMultiplayerRoomCapacity) {
        n.winnerSlot=*value;n.matchPhase=RoomMatchPhase::Finished;++n.roomRevision;
    } else if(f[0]=="!knockout" && f.size()==4) {
        const auto killer=Util::Text::parse_integer<int>(f[3]);
        if(killer && *killer>=-1 && *killer<kMultiplayerRoomCapacity)
            for(auto& m:n.roomMembers) if(m.slot==*value) {m.alive=false;m.killer=*killer;++n.roomRevision;}
    }
    return true;
}
void poll_multiplayer_transfer(NetworkServices& n) {
    if(!n.multiplayerRoom) return;
    const auto now=std::chrono::steady_clock::now();
    if(n.pendingResultEpoch && !n.pendingRoundResult.empty() && now-n.lastResultSend>=std::chrono::milliseconds(100)) {
        const auto parts=round_statistics_parts(n.pendingRoundResult);
        for(std::size_t i=0;i<std::min<std::size_t>(8,parts.size());++i) {
            send_online(n,encode_match_packet(*n.pendingResultEpoch,parts[n.pendingResultCursor++%parts.size()]));
        }
        n.lastResultSend=now;
    }
    const bool runningProbe=n.matchPhase==RoomMatchPhase::Playing && n.boardViewsV2Confirmed;
    if(((n.countdownClockRequested && (n.matchPhase==RoomMatchPhase::Preparing || n.matchPhase==RoomMatchPhase::Countdown)) || runningProbe) &&
       now-n.lastCountdownProbe>=std::chrono::milliseconds(runningProbe?500:100)) {
        const auto probe=++n.countdownProbe;n.countdownProbes.emplace(probe,now);
        while(n.countdownProbes.size()>16) n.countdownProbes.erase(n.countdownProbes.begin());
        send_online(n,encode_match_packet(n.matchEpoch,encode_countdown_clock({probe,std::nullopt})));
        n.lastCountdownProbe=now;
    }
    if(n.matchPhase!=RoomMatchPhase::Playing) return;
    if(now-n.lastAttackSend<std::chrono::milliseconds(250)) return;
    int count=0;for(const auto& [id,wire]:n.outgoingAttacks) {if(++count>32) break;send_online(n,encode_match_packet(n.matchEpoch,wire));}
    send_online(n,encode_match_packet(n.matchEpoch,"!attack-ack;"+std::to_string(n.receivedAttack)));
    n.lastAttackSend=now;
}
std::optional<FramePacket> try_receive_multiplayer_frame(Runtime::GameContext& context,bool pollConnection,std::chrono::microseconds processingBudget) {
    auto& n=network_services(context);if(pollConnection) poll_connection(n);
    auto& pending=context.session.player.multiplayer.pendingSends;
    for(auto& sent:pending) if(auto it=n.actualAttackTargets.find(sent.sequenceId.value());it!=n.actualAttackTargets.end()) sent.targetSlot=it->second;
    while(!n.actualAttackTargets.empty() && n.actualAttackTargets.begin()->first<=n.deliveredAttack) n.actualAttackTargets.erase(n.actualAttackTargets.begin());
    if(!pending.empty() && pending.front().sequenceId.value()<=n.deliveredAttack) {
        FramePacket ack;ack.multiplayer=true;ack.sendId=Runtime::MultiplayerSequenceId::from_wire(n.deliveredAttack);return ack;
    }
    const auto decodeDeadline=std::chrono::steady_clock::now()+processingBudget;
    int decoded=0;
    while(!n.playerInbox.empty() && ++decoded<=64 && std::chrono::steady_clock::now()<decodeDeadline) {
        auto wire=std::move(n.playerInbox.front());n.playerInbox.pop_front();
        // Subscription changes may happen after datagrams enter this queue.
        // Combat and turn messages retain their independent delivery path.
        if(board_visual(wire.data) && !wants_full_board(n,wire.slot))continue;
        const auto setup=n.playerSetups.find(wire.slot);
        const int width=setup==n.playerSetups.end()?10:setup->second.width;
        const int height=setup==n.playerSetups.end()?20:setup->second.visibleHeight;
        auto packet=decode_frame_packet(wire.data,width,height);if(!packet) continue;
        packet->sourceSlot=wire.slot;packet->multiplayer=true;
        if(packet->type==FramePacketType::Attack) {
            const int sequence=packet->receiveId.value();
            if(sequence>n.receivedAttack && sequence<=n.receivedAttack+4096) n.incomingAttacks.emplace(sequence,std::move(*packet));
        } else if(packet->type==FramePacketType::PieceTurn) {
            if(packet->turnEpoch==n.boardSync.epoch) n.peerTurns[wire.slot]=std::max(n.peerTurns[wire.slot],packet->completedTurns);
        } else {
            auto [it,created]=n.peerTimelines.try_emplace(wire.slot);
            if(created) it->second.reset(n.boardSync.epoch);
            if(packet->checkpoint || packet->motion) it->second.insert(std::move(*packet),context.real_now());
            else return packet;
        }
    }
    if(auto it=n.incomingAttacks.find(n.receivedAttack+1);it!=n.incomingAttacks.end()) {
        auto packet=std::move(it->second);n.incomingAttacks.erase(it);++n.receivedAttack;
        send(n,"!attack-ack;"+std::to_string(n.receivedAttack));return packet;
    }
    for(auto& [slot,timeline]:n.peerTimelines) {
        if(auto packet=timeline.next(context.real_now())) {packet->sourceSlot=slot;packet->multiplayer=true;return packet;}
        if(timeline.needs_snapshot()) request_board_snapshot(context);
    }
    // The server forwards snapshot requests without a player envelope.
    for(auto it=n.onlineInbox.begin();it!=n.onlineInbox.end();) {
        if(decode_snapshot_request(*it)) {
            it=n.onlineInbox.erase(it);
            const auto& p=context.session.player;
            send_board_state_frame(context,p,p.presentation.activePose?ActivePieceState::active(*p.presentation.activePose):ActivePieceState::locked(),std::nullopt,true);
        } else ++it;
    }
    return std::nullopt;
}
} // namespace Network
