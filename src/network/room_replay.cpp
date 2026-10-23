#include "tetr/network/room_replay.h"
#include "tetr/network/network_services.h"
#include "tetr/network/hybrid_protocol.h"
#include "tetr/runtime/game_context.h"
#include "tetr/config/user_config.h"
#include "tetr/replay/session_state.h"
#include "tetr/util/text_codec.h"
#include "tetr/util/runtime_directory_lock.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <random>
#include <thread>
#include <condition_variable>
#include <functional>
namespace Network {
namespace {
class ReplayScratchRoot {
    std::unique_ptr<Util::RuntimeDirectoryLock> lock_;
public:
    std::filesystem::path path;
    ReplayScratchRoot() {
        std::error_code error;const auto temporary=std::filesystem::temp_directory_path(error);if(error)return;
        // A marker plus an OS lock distinguishes our abandoned caches from
        // another running client/server and unrelated temporary directories.
        for(std::filesystem::directory_iterator it(temporary,error),end;!error && it!=end;it.increment(error)) {
            const auto name=it->path().filename().u8string();
            if(name.rfind("termino-replay-cache-",0)!=0 || it->is_symlink(error) || !it->is_directory(error))continue;
            std::ifstream marker(it->path()/"format");char tag[64]{};marker.getline(tag,sizeof tag);
            if(std::string_view(tag)!="TERMINO_REPLAY_TEMP_1")continue;
            auto old=std::make_unique<Util::RuntimeDirectoryLock>(it->path()/"owner.lock");
            if(old->acquired()){old.reset();std::error_code ignored;std::filesystem::remove_all(it->path(),ignored);}
        }
        error.clear();std::random_device random;
        for(int attempt=0;attempt<5;++attempt) {
            const auto candidate=temporary/("termino-replay-cache-"+std::to_string(random())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            if(std::filesystem::create_directory(candidate,error)){path=candidate;break;}error.clear();
        }
        if(path.empty())return;
        std::filesystem::permissions(path,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace,error);
        lock_=std::make_unique<Util::RuntimeDirectoryLock>(path/"owner.lock");
        if(!lock_->acquired()){path.clear();return;}
        std::ofstream marker(path/"format");marker<<"TERMINO_REPLAY_TEMP_1\n";marker.close();
        if(!marker){lock_.reset();std::filesystem::remove_all(path,error);path.clear();}
    }
    ~ReplayScratchRoot(){lock_.reset();std::error_code error;if(!path.empty())std::filesystem::remove_all(path,error);}
};
ReplayScratchRoot& replay_scratch_root(){static ReplayScratchRoot root;return root;}
class ReplayIo {
    std::mutex mutex_;
    std::condition_variable available_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_=false;
    std::thread worker_;
public:
    ReplayIo():worker_([this] {
        for(;;) {
            std::function<void()> job;
            {std::unique_lock<std::mutex> lock(mutex_);available_.wait(lock,[&]{return stopping_ || !jobs_.empty();});
             if(jobs_.empty() && stopping_)return;job=std::move(jobs_.front());jobs_.pop_front();}
            try{job();}catch(...){} // File jobs publish their own error state.
        }
    }){}
    ~ReplayIo(){{std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}available_.notify_one();worker_.join();}
    void post(std::function<void()> job){{std::lock_guard<std::mutex> lock(mutex_);jobs_.push_back(std::move(job));}available_.notify_one();}
};
ReplayIo& replay_io(){static ReplayIo io;return io;}
std::string number(std::uint64_t n){return std::to_string(n);}
std::uint64_t elapsed(RoomReplayUpload& upload,const Runtime::GameContext& c) {
    return std::max(upload.lastTime,static_cast<std::uint64_t>(std::max(0.,(c.real_now()-upload.origin).seconds())*1000000.));
}
auto parse(std::string_view value){return Util::Text::parse_integer<std::uint64_t>(value);}
}
std::optional<ReplayMessage> decode_replay_message(std::string_view wire) {
    if(wire.substr(0,11)!="!replay-v1;" || wire.size()>16000)return {};
    const auto fields=Util::Text::split(wire,';');if(fields.size()<3 || fields.size()>15)return {};
    ReplayMessage result;result.action=fields[1];result.token=fields[2];
    for(std::size_t i=3;i<fields.size();++i)result.fields.push_back(fields[i]);return result;
}
std::string replay_message(std::string_view action,std::string_view token,const std::vector<std::string>& fields) {
    std::string result="!replay-v1;";result+=action;result+=';';result+=token;for(const auto& f:fields){result+=';';result+=f;}return result;
}
struct ReplayScratchFile::State {
    std::filesystem::path path;
    std::atomic<std::uint64_t> size{0},digest{14695981039346656037ULL};
    std::atomic<bool> failed{false},alive{true};
    std::mutex cacheMutex;
    bool reading=false;
    std::uint64_t cacheOffset=0;
    std::string cache;
    ~State(){std::error_code error;if(!path.empty())std::filesystem::remove(path,error);}
    void write(std::string_view bytes) {
        if(!alive || failed)return;
        std::ofstream file(path,std::ios::binary|std::ios::app);file.write(bytes.data(),bytes.size());file.close();
        if(!file){failed=true;return;}
        auto hash=digest.load();for(unsigned char c:bytes){hash^=c;hash*=1099511628211ULL;}
        digest.store(hash);size.fetch_add(bytes.size());
    }
    std::string read(std::uint64_t offset,std::size_t length) const {
        const auto stored=size.load();if(failed || offset>stored)return {};length=static_cast<std::size_t>(std::min<std::uint64_t>(length,stored-offset));
        std::ifstream file(path,std::ios::binary);if(!file)return {};file.seekg(static_cast<std::streamoff>(offset));
        std::string result(length,'\0');file.read(result.data(),length);if(static_cast<std::size_t>(file.gcount())!=length)return {};return result;
    }
};
ReplayScratchFile::ReplayScratchFile(bool asynchronous):state_(std::make_shared<State>()),asynchronous_(asynchronous),size(state_->size),digest(state_->digest),failed(state_->failed) {
    static std::atomic<unsigned> sequence{0};std::random_device random;std::error_code error;
    const auto& root=replay_scratch_root().path;
    if(root.empty()){failed=true;return;}
    state_->path=root/("termino-replay-"+number(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+number(random())+"-"+number(sequence++)+".tmp");
    if(asynchronous_)replay_io();
}
ReplayScratchFile::~ReplayScratchFile(){state_->alive=false;}
bool ReplayScratchFile::queue(std::string_view bytes,std::size_t limit) {
    if(failed || bytes.size()>limit || queued_size()>limit-bytes.size())return false;
    pending_.append(bytes.data(),bytes.size());return true;
}
bool ReplayScratchFile::flush() {
    if(failed)return false;if(pending_.empty())return true;
    auto bytes=std::move(pending_);pending_.clear();scheduled_+=bytes.size();
    if(asynchronous_)replay_io().post([state=state_,bytes=std::move(bytes)] {
        try{state->write(bytes);}catch(...){state->failed=true;}
    });else state_->write(bytes);
    return !failed;
}
std::string ReplayScratchFile::read(std::uint64_t offset,std::size_t length) const {
    return state_->read(offset,length);
}
std::optional<std::string> ReplayScratchFile::try_read(std::uint64_t offset,std::size_t length) {
    if(failed || offset>=size.load())return {};
    const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(length,size.load()-offset));
    std::lock_guard<std::mutex> lock(state_->cacheMutex);
    if(offset>=state_->cacheOffset && offset-state_->cacheOffset<=state_->cache.size() && count<=state_->cache.size()-(offset-state_->cacheOffset))
        return state_->cache.substr(static_cast<std::size_t>(offset-state_->cacheOffset),count);
    if(state_->reading)return {};state_->reading=true;
    replay_io().post([state=state_,offset] {
        std::string bytes;try{if(state->alive)bytes=state->read(offset,8*kReplayTransferChunk);}catch(...){state->failed=true;}
        std::lock_guard<std::mutex> guard(state->cacheMutex);state->reading=false;
        state->cacheOffset=offset;state->cache=std::move(bytes);
    });return {};
}
void RoomReplayUpload::append(char kind,std::uint64_t time,std::string_view data) {
    if(final || limited)return;time=std::max(lastTime,time);const auto bytes=Replay::journal_record(kind,time,data);
    if(!file->queue(bytes))limited=true;else lastTime=time;
}
RoomReplayClient& room_replays(NetworkServices& n){if(!n.replays)n.replays=std::make_shared<RoomReplayClient>();return *n.replays;}
void prepare_room_replay_storage(){(void)replay_scratch_root();}
void initialize_room_replay_identity(NetworkServices& n,const std::string& profilePath) {
    if(profilePath.empty())return;
    const auto profile=std::filesystem::u8path(profilePath);
    const auto path=profile.parent_path()/"replay"/("viewer-"+number(Replay::multiplayer_digest(profile.filename().generic_u8string()))+".id");
    Util::FileWriteLock lock(path);if(!lock.acquired())return;
    std::ifstream input(path,std::ios::binary);char magicBytes[64]{},idBytes[64]{};
    input.getline(magicBytes,sizeof magicBytes);input.getline(idBytes,sizeof idBytes);
    std::string id=idBytes;
    if(input && std::string_view(magicBytes)=="TETR_REPLAY_VIEWER_1" && id.size()==32 && Replay::replay_unhex(id,16)) {
        room_replays(n).viewerIdentity=id;return;
    }
    input.close();std::error_code error;if(std::filesystem::exists(path,error) || error)return;
    std::random_device random;std::string bytes;for(int i=0;i<16;++i)bytes+=char(random()&255);id=Replay::replay_hex(bytes);
    auto temporary=path;temporary+=".tmp";std::ofstream output(temporary,std::ios::binary);output<<"TETR_REPLAY_VIEWER_1\n"<<id<<'\n';output.close();
    if(!output)return;std::filesystem::rename(temporary,path,error);
    if(error){std::filesystem::remove(temporary,error);return;}room_replays(n).viewerIdentity=id;
}
void begin_room_replay_recording(Runtime::GameContext& c) {
    auto& n=network_services(c);if(!n.multiplayerRoom || c.config.mode.replayMode || !c.session.roomScene.enabled || c.session.roomScene.spectator)return;
    auto& replay=Replay::session(c);replay.onlineRecording=true;
    auto& state=room_replays(n);if(!state.supported)return;
    const auto key=std::pair{n.roomInstance,c.session.roomScene.epoch};
    auto upload=std::make_shared<RoomReplayUpload>();upload->room=key.first;upload->epoch=key.second;upload->origin=c.real_now();
    upload->actor=Replay::multiplayer_actor_id(upload->epoch,c.session.roomScene.ownSlot);
    replay.onlineIdentity={upload->room,upload->epoch,upload->actor};state.ownActors[key]=upload->actor;
    if(c.user_settings_component() && !c.user_settings().persistencePath.empty())replay.onlineProfile=Replay::multiplayer_digest(UserConfig::profile_identity(c.user_settings()));
    if(n.countdownDeadline)upload->origin=std::min(upload->origin,c.services.clock.at(*n.countdownDeadline));
    upload->append('C',0,Replay::session(c).configPrefix);state.uploads[key]=upload;state.active=upload;
    replay.recordingStart=c.real_now();
    replay.recordingLimit=Replay::kMaxReplayJournalBytes;
    replay.onlineRecording=true;
    replay.recordObserver=[weak=std::weak_ptr<RoomReplayUpload>(upload),&c](std::string_view bytes) {
        if(auto target=weak.lock())target->append('O',elapsed(*target,c),bytes);
    };
    const auto& p=c.session.player;
    record_room_replay_board(c,p,p.presentation.activePose?ActivePieceState::active(*p.presentation.activePose):ActivePieceState::spawn());
}
void finish_room_replay_recording(Runtime::GameContext& c,std::string_view statistics) {
    auto& n=network_services(c);if(!n.replays || !n.replays->active)return;auto upload=n.replays->active;
    if(upload->epoch!=c.session.roomScene.epoch)return;
    const auto& p=c.session.player;
    record_room_replay_board(c,p,p.presentation.activePose?ActivePieceState::active(*p.presentation.activePose):ActivePieceState::locked());
    upload->append('F',elapsed(*upload,c),statistics);upload->final=true;
    Replay::session(c).recordObserver={};n.replays->active.reset();
}
void record_room_replay_board(Runtime::GameContext& c,const Runtime::PlayerSessionState& p,ActivePieceState active,std::optional<RemotePlacementSnapshot> placement) {
    auto* n=c.network_component();if(!n || !n->replays || !n->replays->active || c.config.mode.replayMode)return;
    auto& upload=*n->replays->active;if(upload.final)return;
    const auto board=encode_board_state(c,p,active,placement);const auto hash=board_state_fingerprint(board);
    const auto clock=capture_player_clock(c,p);
    const auto piece=p.pieces.bag.empty()?Piece::Type::none():p.pieces.bag.front().type;
    if(hash!=upload.boardFingerprint || placement || upload.motionSequence==0 ||
       piece!=upload.piece || (active.kind==ActivePieceStateKind::Active && !upload.pose) ||
       (active.kind==ActivePieceStateKind::Locked && upload.pose) || !upload.clockRate || *upload.clockRate!=clock.rate) {
        upload.append('B',elapsed(upload,c),number(p.gameplay.playfield.width)+";"+number(p.gameplay.playfield.visibleHeight)+";"+number(p.gameplay.playfield.nextCount)+";"+board);
        upload.boardFingerprint=hash;++upload.motionSequence;
        upload.clockRate=clock.rate;
        upload.piece=p.pieces.bag.empty()?Piece::Type::none():p.pieces.bag.front().type;
        upload.pose=active.kind==ActivePieceStateKind::Active?std::optional<Piece::Pose>(active.pose):std::nullopt;
    } else if(active.kind==ActivePieceStateKind::Active)record_room_replay_motion(c,p,active.pose);
}
void record_room_replay_motion(Runtime::GameContext& c,const Runtime::PlayerSessionState& p,Piece::Pose pose) {
    auto* n=c.network_component();if(!n || !n->replays || !n->replays->active || c.config.mode.replayMode)return;
    auto& upload=*n->replays->active;const auto piece=p.pieces.bag.empty()?Piece::Type::none():p.pieces.bag.front().type;
    if(piece!=upload.piece || !upload.pose){record_room_replay_board(c,p,ActivePieceState::active(pose));return;}
    if(*upload.pose==pose)return;
    PieceMotionFrame motion;motion.stamp={1,++upload.motionSequence,0,1,upload.boardFingerprint};motion.piece=piece;
    motion.kind=PieceMotionKind::Translate;motion.result=pose;motion.clock=capture_player_clock(c,p);
    upload.append('M',elapsed(upload,c),encode_piece_motion(motion));upload.pose=pose;
}
void request_room_replay_list(NetworkServices& n) {
    auto& r=room_replays(n);r.listRoom=n.roomInstance;r.listRequest=++r.nextRequest;r.listPending=true;r.lastListSend={};
}
void request_room_replay_download(NetworkServices& n,std::uint64_t room,std::uint64_t epoch) {
    auto& r=room_replays(n);r.download=RoomReplayDownload{};auto& d=*r.download;d.room=room;d.epoch=epoch;d.id=++r.nextRequest;
}
bool handle_room_replay_message(NetworkServices& n,std::string_view wire) {
    if(wire.substr(0,11)!="!replay-v1;")return false;const auto message=decode_replay_message(wire);
    if(!message || message->token!=n.connectionToken)return true;auto& r=room_replays(n);const auto& f=message->fields;
    if(message->action=="hello" && f.empty()){r.supported=true;return true;}
    if(f.size()<2)return true;const auto room=parse(f[0]),epoch=parse(f[1]);if(!room||!epoch)return true;
    const auto key=std::pair{*room,*epoch};
    if(message->action=="identity" && f.size()==3) {
        if(const auto actor=parse(f[2])){r.ownActors[key]=*actor;if(const auto it=r.uploads.find(key);it!=r.uploads.end())it->second->actor=*actor;}return true;
    }
    if(message->action=="ended" && f.size()==3) {
        if(const auto reason=parse(f[2]);reason && *reason<=2)r.ended[key]=static_cast<Replay::RoundEndReason>(*reason);return true;
    }
    if(message->action=="ack" && f.size()==3) {
        if(const auto offset=parse(f[2]);offset)if(const auto it=r.uploads.find(key);it!=r.uploads.end()) {
            auto& u=*it->second;if(*offset<=u.file->size){u.acknowledged=std::max(u.acknowledged,*offset);u.cursor=std::max(u.cursor,u.acknowledged);}
        }return true;
    }
    if(message->action=="stored" && f.size()==2) {
        if(const auto it=r.uploads.find(key);it!=r.uploads.end())it->second->stored=true;return true;
    }
    if(message->action=="discarded" && f.size()==2) {
        if(const auto it=r.uploads.find(key);it!=r.uploads.end())it->second->stored=true;return true;
    }
    if(message->action=="list" && f.size()==3 && *room==r.listRoom && *epoch==r.listRequest) {
        if(auto bytes=Replay::replay_unhex(f[2],8192))if(auto list=Replay::decode_room_replay_list(*bytes)){r.list=std::move(*list);r.listPending=false;++r.listRevision;}return true;
    }
    if(!r.download || r.download->room!=*room || r.download->epoch!=*epoch || f.size()<3)return true;
    auto& d=*r.download;const auto id=parse(f[2]);if(!id||*id!=d.id)return true;
    if(message->action=="status" && f.size()==4) {d.status=f[3];if(d.status!="collecting")d.failed=true;return true;}
    if(message->action=="data" && f.size()==8 && !d.ready && !d.failed) {
        const auto total=parse(f[3]),digest=parse(f[4]),offset=parse(f[5]),viewer=parse(f[6]);
        const auto bytes=Replay::replay_unhex(f[7],kReplayTransferChunk);
        if(!total||!*total||*total>Replay::kMaxMultiplayerReplayBytes||!digest||!offset||!viewer||!bytes || bytes->empty() ||
           *offset>*total || bytes->size()>*total-*offset)return true;
        if(d.total && (d.total!=*total || d.digest!=*digest || d.viewer!=*viewer)){d.failed=true;d.status="changed";return true;}
        d.total=*total;d.digest=*digest;d.viewer=*viewer;d.status.clear();
        if(*offset>=d.bytes.size() && *offset<d.bytes.size()+16*kReplayTransferChunk)d.pending.emplace(*offset,*bytes);
        for(;;){auto found=d.pending.find(d.bytes.size());if(found==d.pending.end())break;d.bytes+=found->second;d.pending.erase(found);}
        if(d.bytes.size()==d.total) {
            if(Replay::multiplayer_digest(d.bytes)!=d.digest){d.failed=true;d.status="checksum";return true;}
            if(auto archive=Replay::decode_multiplayer_replay(d.bytes)) {
                archive->viewer=d.viewer;
                if(!archive->viewer)if(const auto own=r.ownActors.find(key);own!=r.ownActors.end() &&
                    std::any_of(archive->tracks.begin(),archive->tracks.end(),[&](const auto& track){return track.actor==own->second;}))archive->viewer=own->second;
                d.bytes=Replay::encode_multiplayer_replay(*archive);d.ready=true;
            }else{d.failed=true;d.status="invalid";}
        }
    }return true;
}
void poll_room_replay_transfer(NetworkServices& n) {
    if(n.connection!=ConnectionState::Connected || n.protocolVersion<4)return;auto& r=room_replays(n);const auto now=std::chrono::steady_clock::now();
    if(!r.supported && now-r.lastHello>=std::chrono::seconds(1)) {
        send_online(n,replay_message("hello",n.connectionToken,r.viewerIdentity.empty()?std::vector<std::string>{}:std::vector<std::string>{r.viewerIdentity}));r.lastHello=now;
    }
    if(!r.supported)return;
    if(r.listPending && now-r.lastListSend>=std::chrono::milliseconds(250)) {
        send_online(n,replay_message("list",n.connectionToken,{number(r.listRoom),number(r.listRequest)}));r.lastListSend=now;
    }
    for(auto it=r.uploads.begin();it!=r.uploads.end();) {
        auto& u=*it->second;
        if(u.stored){it=r.uploads.erase(it);continue;}
        ++it;if(!u.file->flush()){u.limited=true;u.stored=true;continue;}
        if(!u.actor && now-u.lastIdentity>=std::chrono::milliseconds(250)) {
            send_online(n,replay_message("identity",n.connectionToken,{number(u.room),number(u.epoch)}));u.lastIdentity=now;
        }
        if(!u.actor || now-u.lastSend<std::chrono::milliseconds(20))continue;
        // A small sliding window shares the existing socket, with bounded work
        // per poll. Missing/duplicated UDP chunks converge on a cumulative ACK.
        if(u.cursor>=u.file->size || u.cursor>=u.acknowledged+8*kReplayTransferChunk) {
            if(now-u.lastSend<std::chrono::milliseconds(150))continue;u.cursor=u.acknowledged;
        }
        u.lastSend=now;
        for(int count=0;count<4 && u.cursor<u.file->size && u.cursor<u.acknowledged+8*kReplayTransferChunk;++count) {
            const auto bytes=u.file->try_read(u.cursor,kReplayTransferChunk);if(!bytes || bytes->empty())break;
            send_online(n,replay_message("put",n.connectionToken,{number(u.room),number(u.epoch),number(u.cursor),number(Replay::multiplayer_digest(*bytes)),Replay::replay_hex(*bytes)}));u.cursor+=bytes->size();
        }
        if(u.final && u.acknowledged==u.file->size && u.file->queued_size()==u.file->size)
            send_online(n,replay_message("finish",n.connectionToken,{number(u.room),number(u.epoch),number(u.file->size),number(u.file->digest),number(u.lastTime),u.limited?"0":"1"}));
    }
    if(r.download && !r.download->ready && !r.download->failed && now-r.download->lastRequest>=std::chrono::milliseconds(100)) {
        auto& d=*r.download;send_online(n,replay_message("get",n.connectionToken,{number(d.room),number(d.epoch),number(d.id),number(d.bytes.size())}));d.lastRequest=now;
    }
    const auto trim=[](auto& map){while(map.size()>64)map.erase(map.begin());};trim(r.ownActors);trim(r.recentPaths);trim(r.ended);
}
}
