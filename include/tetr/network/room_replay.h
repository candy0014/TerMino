#ifndef TETR_NETWORK_ROOM_REPLAY_H
#define TETR_NETWORK_ROOM_REPLAY_H
#include "tetr/replay/multiplayer_codec.h"
#include "tetr/runtime/time.h"
#include "tetr/network/board_state_codec.h"
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <atomic>
namespace Runtime {class GameContext;struct PlayerSessionState;}
namespace Network {
struct NetworkServices;
inline constexpr std::size_t kReplayTransferChunk=480;
struct ReplayMessage {std::string action,token;std::vector<std::string_view> fields;};
std::optional<ReplayMessage> decode_replay_message(std::string_view wire);
std::string replay_message(std::string_view action,std::string_view token,const std::vector<std::string>& fields={});
// Scratch files are owned by the transfer, never placed in a user's profile.
// Gameplay queues bytes in memory; network polling flushes bounded batches.
class ReplayScratchFile {
    struct State;
    std::shared_ptr<State> state_;
    std::string pending_;
    std::uint64_t scheduled_=0;
    bool asynchronous_=false;
public:
    std::atomic<std::uint64_t>& size;
    std::atomic<std::uint64_t>& digest;
    std::atomic<bool>& failed;
    explicit ReplayScratchFile(bool asynchronous=false);
    ~ReplayScratchFile();
    ReplayScratchFile(const ReplayScratchFile&)=delete;
    ReplayScratchFile& operator=(const ReplayScratchFile&)=delete;
    bool queue(std::string_view bytes,std::size_t limit=Replay::kMaxReplayJournalBytes);
    bool flush();
    std::uint64_t queued_size() const {return scheduled_+pending_.size();}
    std::string read(std::uint64_t offset,std::size_t length) const;
    std::optional<std::string> try_read(std::uint64_t offset,std::size_t length);
};
struct RoomReplayUpload {
    std::uint64_t room=0,epoch=0,actor=0,acknowledged=0,cursor=0,lastTime=0;
    Runtime::TimePoint origin{};
    std::shared_ptr<ReplayScratchFile> file=std::make_shared<ReplayScratchFile>(true);
    std::chrono::steady_clock::time_point lastSend{},lastIdentity{};
    std::uint64_t boardFingerprint=0,motionSequence=0;
    std::optional<double> clockRate;
    std::optional<Piece::Pose> pose;
    Piece::Type piece=Piece::Type::none();
    bool final=false,stored=false,limited=false;
    void append(char kind,std::uint64_t time,std::string_view data);
};
struct RoomReplayDownload {
    std::uint64_t room=0,epoch=0,id=0,total=0,digest=0,viewer=0;
    std::string bytes,status;
    std::map<std::uint64_t,std::string> pending;
    bool ready=false,failed=false;
    std::chrono::steady_clock::time_point lastRequest{};
};
struct RoomReplayClient {
    std::string viewerIdentity;
    bool supported=false;
    std::chrono::steady_clock::time_point lastHello{},lastListSend{};
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::shared_ptr<RoomReplayUpload>> uploads;
    std::shared_ptr<RoomReplayUpload> active;
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::uint64_t> ownActors;
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::string> recentPaths;
    std::map<std::pair<std::uint64_t,std::uint64_t>,Replay::RoundEndReason> ended;
    std::vector<Replay::RoomReplaySummary> list;
    std::uint64_t listRoom=0,listRequest=0,nextRequest=0,listRevision=0;
    bool listPending=false;
    std::optional<RoomReplayDownload> download;
};
RoomReplayClient& room_replays(NetworkServices& services);
void prepare_room_replay_storage();
void initialize_room_replay_identity(NetworkServices&,const std::string& profilePath);
bool handle_room_replay_message(NetworkServices&,std::string_view wire);
void poll_room_replay_transfer(NetworkServices&);
void request_room_replay_list(NetworkServices&);
void request_room_replay_download(NetworkServices&,std::uint64_t room,std::uint64_t epoch);
void begin_room_replay_recording(Runtime::GameContext&);
void finish_room_replay_recording(Runtime::GameContext&,std::string_view statistics);
void record_room_replay_board(Runtime::GameContext&,const Runtime::PlayerSessionState&,
    ActivePieceState active,std::optional<RemotePlacementSnapshot> placement={});
void record_room_replay_motion(Runtime::GameContext&,const Runtime::PlayerSessionState&,Piece::Pose);
}
#endif
