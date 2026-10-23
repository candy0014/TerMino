#include "tetr/replay/multiplayer_codec.h"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
namespace Replay {
namespace {
constexpr std::string_view magic="TETR_MULTIPLAYER_REPLAY_1:";
void put(std::string& out,std::uint64_t n) {do{const auto b=n&127;n>>=7;out+=char(b|(n?128:0));}while(n);}
bool get(std::string_view& in,std::uint64_t& n) {
    n=0;for(int i=0;i<10;++i) {if(in.empty())return false;const auto b=static_cast<unsigned char>(in.front());in.remove_prefix(1);
        if(i==9 && (b&254))return false;n|=std::uint64_t(b&127)<<(7*i);if(!(b&128))return !i || b;}
    return false;
}
void put_string(std::string& out,std::string_view s) {put(out,s.size());out.append(s.data(),s.size());}
bool get_string(std::string_view& in,std::string& out,std::size_t max) {
    std::uint64_t n=0;if(!get(in,n)||n>max||n>in.size())return false;out.assign(in.substr(0,n));in.remove_prefix(n);return true;
}
}
bool MultiplayerReplay::complete() const {
    return !tracks.empty() && std::all_of(tracks.begin(),tracks.end(),[](const auto& t){return t.complete;});
}
std::uint64_t multiplayer_digest(std::string_view s) noexcept {
    std::uint64_t hash=14695981039346656037ULL;for(unsigned char c:s){hash^=c;hash*=1099511628211ULL;}return hash;
}
bool is_multiplayer_replay(std::string_view s) noexcept {return s.substr(0,magic.size())==magic;}
std::uint64_t multiplayer_actor_id(std::uint64_t epoch,int slot) noexcept {
    const auto id=(epoch<<5)^std::uint64_t(slot+1);return id?id:std::uint64_t(1)<<63;
}
std::optional<std::array<std::uint64_t,3>> multiplayer_replay_identity(std::string_view in) {
    if(!is_multiplayer_replay(in))return {};in.remove_prefix(magic.size());
    std::array<std::uint64_t,3> result{};std::uint64_t ignored=0;
    if(!get(in,result[0])||!get(in,result[1])||!get(in,ignored)||!get(in,ignored)||!get(in,result[2]))return {};return result;
}
std::string encode_multiplayer_replay(const MultiplayerReplay& r) {
    if(r.tracks.empty() || r.tracks.size()>32)throw std::length_error("Invalid multiplayer replay roster");
    std::string out(magic);put(out,r.room);put(out,r.epoch);put(out,r.startedAt);put(out,r.duration);put(out,r.viewer);
    put(out,r.winner+1);put(out,static_cast<unsigned>(r.reason));
    if(r.targets.size()>1000000)throw std::length_error("Replay event limit exceeded");
    put(out,r.targets.size());for(const auto& event:r.targets){put(out,event.time);put(out,event.slot);put(out,event.target+1);}
    put(out,r.tracks.size());
    for(const auto& t:r.tracks) {
        if(t.journal.size()>kMaxReplayJournalBytes)throw std::length_error("Replay journal too large");
        put(out,t.actor);put(out,t.slot);put_string(out,t.name);put_string(out,t.setup);put_string(out,t.visibility);
        put(out,t.complete);put(out,t.eliminatedAt.has_value());if(t.eliminatedAt)put(out,*t.eliminatedAt);
        put(out,t.killer+1);put(out,t.endedAt);put_string(out,t.journal);
        if(out.size()>kMaxMultiplayerReplayBytes)throw std::length_error("Multiplayer replay too large");
    }return out;
}
std::optional<MultiplayerReplay> decode_multiplayer_replay(std::string_view in) {
    if(!is_multiplayer_replay(in) || in.size()>kMaxMultiplayerReplayBytes)return {};in.remove_prefix(magic.size());
    MultiplayerReplay r;std::uint64_t winner=0,reason=0,count=0,eventCount=0;
    if(!get(in,r.room)||!r.room||!get(in,r.epoch)||!r.epoch||!get(in,r.startedAt)||!get(in,r.duration)||
       !get(in,r.viewer)||!get(in,winner)||winner>32||!get(in,reason)||reason>2||!get(in,eventCount)||eventCount>1000000)return {};
    std::uint64_t previousEvent=0;
    for(std::uint64_t i=0;i<eventCount;++i){std::uint64_t time=0,slot=0,target=0;
        if(!get(in,time)||time<previousEvent||time>r.duration||!get(in,slot)||slot>=32||!get(in,target)||target>32)return {};
        previousEvent=time;r.targets.push_back({time,int(slot),int(target)-1});}
    if(!get(in,count)||!count||count>32)return {};
    r.winner=int(winner)-1;r.reason=static_cast<RoundEndReason>(reason);std::set<std::uint64_t> actors;std::set<int> slots;
    for(std::uint64_t i=0;i<count;++i) {
        MultiplayerReplayTrack t;std::uint64_t slot=0,complete=0,eliminated=0,killer=0;
        if(!get(in,t.actor)||!t.actor||!actors.insert(t.actor).second||!get(in,slot)||slot>=32||!slots.insert(int(slot)).second||
           !get_string(in,t.name,192)||!get_string(in,t.setup,8192)||!get_string(in,t.visibility,1024)||
           !get(in,complete)||complete>1||!get(in,eliminated)||eliminated>1)return {};
        t.slot=int(slot);t.complete=complete;
        if(eliminated){std::uint64_t time=0;if(!get(in,time)||time>r.duration)return {};t.eliminatedAt=time;}
        if(!get(in,killer)||killer>32||!get(in,t.endedAt)||t.endedAt>r.duration||!get_string(in,t.journal,kMaxReplayJournalBytes))return {};
        t.killer=int(killer)-1;
        auto journal=std::string_view(t.journal);std::uint64_t previous=0;bool config=false,final=false;
        while(!journal.empty()) {
            JournalRecord record;if(!take_journal_record(journal,record)||record.time<previous||record.time>r.duration)return {};
            previous=record.time;
            if(record.kind=='C'){if(config||record.time!=0)return {};config=true;}
            else if(!config || final)return {};
            if(record.kind=='F')final=true;
        }
        if(t.complete && (!config || !final))return {};
        r.tracks.push_back(std::move(t));
    }
    if(!in.empty() || (r.viewer && !actors.count(r.viewer)) || (r.winner>=0 && !slots.count(r.winner)))return {};
    for(const auto& event:r.targets)if(!slots.count(event.slot) || (event.target>=0 && !slots.count(event.target)))return {};
    return r;
}
std::string journal_record(char kind,std::uint64_t time,std::string_view data) {
    std::string out(1,kind);put(out,time);put_string(out,data);return out;
}
bool take_journal_record(std::string_view& bytes,JournalRecord& record) {
    if(bytes.empty())return false;auto in=bytes;record.kind=in.front();in.remove_prefix(1);
    if(std::string_view("COBMF").find(record.kind)==std::string_view::npos)return false;
    std::uint64_t count=0;if(!get(in,record.time)||!get(in,count)||count>in.size())return false;
    record.data=in.substr(0,count);in.remove_prefix(count);bytes=in;return true;
}
std::string encode_room_replay_list(const std::vector<RoomReplaySummary>& list) {
    std::string out;put(out,list.size());for(const auto& r:list){put(out,r.epoch);put(out,r.startedAt);put(out,r.duration);put(out,r.players);put(out,r.complete);put(out,r.collecting);put(out,static_cast<unsigned>(r.reason));}return out;
}
std::optional<std::vector<RoomReplaySummary>> decode_room_replay_list(std::string_view in) {
    std::uint64_t count=0;if(!get(in,count)||count>kRoomReplayLimit)return {};std::vector<RoomReplaySummary> result;std::set<std::uint64_t> seen;
    for(std::uint64_t i=0;i<count;++i){RoomReplaySummary r;std::uint64_t players=0,complete=0,collecting=0,reason=0;
        if(!get(in,r.epoch)||!r.epoch||!seen.insert(r.epoch).second||!get(in,r.startedAt)||!get(in,r.duration)||
           !get(in,players)||!players||players>32||!get(in,complete)||complete>1||!get(in,collecting)||collecting>1||!get(in,reason)||reason>2)return {};
        r.players=unsigned(players);r.complete=complete;r.collecting=collecting;r.reason=static_cast<RoundEndReason>(reason);result.push_back(r);}
    if(!in.empty())return {};return result;
}
std::string replay_hex(std::string_view bytes) {constexpr char digits[]="0123456789abcdef";std::string result;result.reserve(bytes.size()*2);for(unsigned char c:bytes){result+=digits[c>>4];result+=digits[c&15];}return result;}
std::optional<std::string> replay_unhex(std::string_view hex,std::size_t limit) {
    if(hex.size()%2 || hex.size()/2>limit)return {};const auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    std::string result;result.reserve(hex.size()/2);for(std::size_t i=0;i<hex.size();i+=2){const int a=digit(hex[i]),b=digit(hex[i+1]);if(a<0||b<0)return {};result+=char(a*16+b);}return result;
}
}
