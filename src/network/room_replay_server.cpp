#include "tetr/network/relay_server.h"
#include "tetr/network/room_replay.h"
#include "tetr/util/text_codec.h"
#include <algorithm>
#include <chrono>
#include <limits>
namespace Network {
struct RoomReplayArchiveState {
    struct Track {
        std::string owner,token,viewerIdentity;
        bool capable=false;
        std::shared_ptr<ReplayScratchFile> file=std::make_shared<ReplayScratchFile>();
    };
    Replay::MultiplayerReplay replay;
    std::map<int,Track> tracks;
    std::chrono::steady_clock::time_point start{},finished{};
    bool ended=false,sealed=false;
    std::shared_ptr<ReplayScratchFile> download;
    std::array<int,32> targets;
    RoomReplayArchiveState(){targets.fill(-2);}
};
namespace {
std::string n(std::uint64_t value){return std::to_string(value);}
auto integer(std::string_view value){return Util::Text::parse_integer<std::uint64_t>(value);}
std::uint64_t micros(std::chrono::steady_clock::duration elapsed) {
    return std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}
bool collecting(const RoomReplayArchiveState& a) {
    if(!a.ended || a.sealed)return !a.ended;
    if(a.replay.complete())return false;
    const bool possible=std::any_of(a.replay.tracks.begin(),a.replay.tracks.end(),[&](const auto& track){return !track.complete && a.tracks.at(track.slot).capable;});
    return possible && std::chrono::steady_clock::now()-a.finished<std::chrono::seconds(15);
}
bool seal(RoomReplayArchiveState& a) {
    if(a.sealed)return bool(a.download);
    auto& r=a.replay;
    for(auto& track:r.tracks) {
        const auto& file=*a.tracks.at(track.slot).file;
        auto bytes=file.read(0,Replay::kMaxReplayJournalBytes);auto remaining=std::string_view(bytes);
        std::size_t valid=0;std::uint64_t previous=0;
        while(!remaining.empty()) {
            Replay::JournalRecord record;
            if(!Replay::take_journal_record(remaining,record)||record.time<previous)break;
            previous=record.time;valid=bytes.size()-remaining.size();
        }
        if(valid!=bytes.size())track.complete=false;
        bytes.resize(valid);track.journal=std::move(bytes);
        r.duration=std::max({r.duration,track.endedAt,previous});
    }
    try {
        const auto bytes=Replay::encode_multiplayer_replay(r);
        if(!Replay::decode_multiplayer_replay(bytes))return false;
        a.download=std::make_shared<ReplayScratchFile>();
        if(!a.download->queue(bytes,Replay::kMaxMultiplayerReplayBytes)||!a.download->flush()){a.download.reset();return false;}
        a.sealed=true;
        // Scratch journals remain for duplicate-finish ACKs. The archive index
        // itself never retains the assembled multi-player payload in RAM.
        for(auto& track:r.tracks){track.journal.clear();track.journal.shrink_to_fit();}
        return true;
    }catch(const std::length_error&){return false;}
}
}
void RelayServer::start_room_replay(Room& room,std::optional<Clock::time_point> started) {
    if(!room.multiplayer || room.phase!=Phase::Playing || room.replays.count(room.matchEpoch))return;
    auto archive=std::make_shared<RoomReplayArchiveState>();archive->start=started.value_or(Clock::now());
    auto& r=archive->replay;r.room=room.instance;r.epoch=room.matchEpoch;
    r.startedAt=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for(int slot=0;slot<room.capacity;++slot)if(room.participating[slot] && !room.players[slot].empty()) {
        const auto& client=clients_.at(room.players[slot]);Replay::MultiplayerReplayTrack track;
        track.actor=Replay::multiplayer_actor_id(room.matchEpoch,slot);
        track.slot=slot;track.name=client.username;track.setup=room.setup[slot];r.tracks.push_back(std::move(track));
        auto& state=archive->tracks[slot];state.owner=room.players[slot];state.token=client.token;state.capable=client.roomReplayCapable;
        state.viewerIdentity=client.replayViewerIdentity;
    }
    if(r.tracks.empty())return;room.replays[room.matchEpoch]=archive;
    record_room_replay_targets(room);
    // The live round is separate from the last 30 ended rounds.
    while(room.replays.size()>Replay::kRoomReplayLimit+1)room.replays.erase(room.replays.begin());
}
void RelayServer::finish_room_replay(Room& room,Replay::RoundEndReason reason) {
    const auto found=room.replays.find(room.matchEpoch);if(found==room.replays.end() || found->second->ended)return;
    auto& a=*found->second;a.ended=true;a.finished=Clock::now();a.replay.duration=micros(a.finished-a.start);
    a.replay.reason=reason;a.replay.winner=reason==Replay::RoundEndReason::Normal?room.winner:-1;
    for(const auto& key:room.players)if(const auto client=clients_.find(key);client!=clients_.end() && client->second.roomReplayCapable)
        socket_.send(client->second.address,replay_message("ended",client->second.token,{n(room.instance),n(room.matchEpoch),n(static_cast<unsigned>(reason))}));
    while(room.replays.size()>Replay::kRoomReplayLimit)room.replays.erase(room.replays.begin());
}
void RelayServer::record_room_replay_elimination(Room& room,int slot) {
    const auto found=room.replays.find(room.matchEpoch);if(found==room.replays.end())return;auto& a=*found->second;
    for(auto& track:a.replay.tracks)if(track.slot==slot){track.eliminatedAt=micros(Clock::now()-a.start);track.killer=room.killer[slot];break;}
}
void RelayServer::record_room_replay_targets(Room& room) {
    const auto found=room.replays.find(room.matchEpoch);if(found==room.replays.end() || found->second->ended)return;
    auto& a=*found->second;const auto time=micros(Clock::now()-a.start);
    for(const auto& track:a.replay.tracks)if(a.targets[track.slot]!=room.target[track.slot] && a.replay.targets.size()<1000000) {
        a.targets[track.slot]=room.target[track.slot];a.replay.targets.push_back({time,track.slot,room.target[track.slot]});
    }
}
bool RelayServer::handle_room_replay(Client& client,std::string_view wire) {
    if(wire.substr(0,11)!="!replay-v1;")return false;
    const auto message=decode_replay_message(wire);if(!message || message->token!=client.token)return true;
    client.lastSeen=Clock::now();const auto reply=[&](std::string_view action,const std::vector<std::string>& fields={}){socket_.send(client.address,replay_message(action,client.token,fields));};
    const auto& f=message->fields;
    if(message->action=="hello" && f.size()<=1) {
        if(!f.empty() && (f[0].size()!=32 || !Replay::replay_unhex(f[0],16)))return true;
        client.roomReplayCapable=true;if(!f.empty())client.replayViewerIdentity=f[0];reply("hello");return true;
    }
    if(!client.roomReplayCapable || f.size()<2)return true;
    const auto roomId=integer(f[0]),epoch=integer(f[1]);if(!roomId||!epoch)return true;
    auto found=std::find_if(rooms_.begin(),rooms_.end(),[&](const auto& entry){return entry.second.instance==*roomId;});
    const bool member=found!=rooms_.end() && client.room==found->first;
    if(message->action=="list" && f.size()==2) {
        if(!member)return true;
        auto& room=found->second;advance_scheduled_countdown(room,found->first);std::vector<Replay::RoomReplaySummary> list;
        for(auto it=room.replays.rbegin();it!=room.replays.rend() && list.size()<Replay::kRoomReplayLimit;++it)if(it->second->ended) {
            const auto& a=*it->second;const auto& r=a.replay;
            list.push_back({r.epoch,r.startedAt,r.duration,static_cast<unsigned>(r.tracks.size()),r.complete(),collecting(a),r.reason});
        }
        reply("list",{n(*roomId),n(*epoch),Replay::replay_hex(Replay::encode_room_replay_list(list))});return true;
    }
    const auto status=[&](const std::string& value){if(f.size()>=3)reply("status",{n(*roomId),n(*epoch),std::string(f[2]),value});};
    if(found==rooms_.end()){status("gone");reply("discarded",{n(*roomId),n(*epoch)});return true;}
    auto& room=found->second;advance_scheduled_countdown(room,found->first);
    const auto history=room.replays.find(*epoch);
    if(history==room.replays.end()){status("gone");reply("discarded",{n(*roomId),n(*epoch)});return true;}auto& a=*history->second;
    const auto own=std::find_if(a.replay.tracks.begin(),a.replay.tracks.end(),[&](const auto& track){const auto& state=a.tracks.at(track.slot);return state.owner==sender_key(client.address) && state.token==client.token;});
    auto viewer=own;
    // The persistent anonymous identity only selects a viewing perspective.
    // Upload authorization above/below still requires the original connection.
    if(viewer==a.replay.tracks.end() && !client.replayViewerIdentity.empty())
        viewer=std::find_if(a.replay.tracks.begin(),a.replay.tracks.end(),[&](const auto& track){return a.tracks.at(track.slot).viewerIdentity==client.replayViewerIdentity;});
    const auto actor=viewer==a.replay.tracks.end()?0:viewer->actor;
    if(message->action=="identity" && f.size()==2) {
        if(own!=a.replay.tracks.end())a.tracks.at(own->slot).capable=true;
        if(member || actor)reply("identity",{n(*roomId),n(*epoch),n(actor)});
        if(a.ended && (member || actor))reply("ended",{n(*roomId),n(*epoch),n(static_cast<unsigned>(a.replay.reason))});return true;
    }
    if(message->action=="put" && f.size()==5 && own!=a.replay.tracks.end()) {
        auto& target=a.tracks.at(own->slot);if(a.sealed || own->complete)return true;target.capable=true;
        const auto offset=integer(f[2]),digest=integer(f[3]);const auto bytes=Replay::replay_unhex(f[4],kReplayTransferChunk);
        if(!offset||!digest||!bytes||bytes->empty()||Replay::multiplayer_digest(*bytes)!=*digest)return true;
        if(*offset==target.file->size) {
            std::uint64_t total=0;for(const auto& entry:a.tracks)total+=entry.second.file->size;
            if(total+bytes->size()>Replay::kMaxMultiplayerReplayBytes-65536 || !target.file->queue(*bytes) || !target.file->flush())return true;
        }
        reply("ack",{n(*roomId),n(*epoch),n(target.file->size)});return true;
    }
    if(message->action=="finish" && f.size()==6 && own!=a.replay.tracks.end()) {
        const auto bytes=integer(f[2]),digest=integer(f[3]),end=integer(f[4]),complete=integer(f[5]);
        const auto& file=*a.tracks.at(own->slot).file;
        if(!bytes||!digest||!end||!complete||*complete>1||*bytes!=file.size||*digest!=file.digest||*end>86400ULL*365*1000000)return true;
        if(!a.sealed) {
            const auto journal=file.read(0,Replay::kMaxReplayJournalBytes);auto remaining=std::string_view(journal);
            std::uint64_t previous=0;bool config=false,final=false,endAll=false,valid=true;
            while(!remaining.empty()) {
                Replay::JournalRecord record;if(!Replay::take_journal_record(remaining,record)||record.time<previous||record.time>*end||final){valid=false;break;}
                if(record.kind=='C'){if(config || record.time)valid=false;config=true;}
                else if(!config)valid=false;
                if(record.kind=='O' && record.data.find("{end_all,")!=std::string_view::npos)endAll=true;
                if(record.kind=='F')final=bool(decode_round_statistics(record.data));previous=record.time;
            }
            own->complete=*complete && valid && config && final && endAll;own->endedAt=*end;
            a.replay.duration=std::max(a.replay.duration,*end);
        }
        reply("stored",{n(*roomId),n(*epoch)});return true;
    }
    if(message->action=="get" && f.size()==4) {
        if(!member){status("gone");return true;}
        const auto request=integer(f[2]),offset=integer(f[3]);if(!request||!offset)return true;
        if(collecting(a)){status("collecting");return true;}
        if(!seal(a)){status("invalid");return true;}
        if(*offset>=a.download->size)return true;
        for(int count=0;count<8 && *offset+count*kReplayTransferChunk<a.download->size;++count) {
            const auto at=*offset+count*kReplayTransferChunk;const auto bytes=a.download->read(at,kReplayTransferChunk);
            reply("data",{n(*roomId),n(*epoch),n(*request),n(a.download->size),n(a.download->digest),n(at),n(actor),Replay::replay_hex(bytes)});
        }return true;
    }
    return true;
}
}
