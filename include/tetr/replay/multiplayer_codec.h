#ifndef TETR_REPLAY_MULTIPLAYER_CODEC_H
#define TETR_REPLAY_MULTIPLAYER_CODEC_H
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <array>

namespace Replay {
inline constexpr std::size_t kRoomReplayLimit=30;
inline constexpr std::size_t kMaxReplayJournalBytes=64*1024*1024;
inline constexpr std::size_t kMaxMultiplayerReplayBytes=256*1024*1024;
enum class RoundEndReason { Normal, HostStopped, Abandoned };
struct MultiplayerReplayTrack {
    std::uint64_t actor=0;
    int slot=-1;
    std::string name,setup,visibility,journal;
    bool complete=false;
    std::optional<std::uint64_t> eliminatedAt;
    int killer=-1;
    std::uint64_t endedAt=0;
};
struct MultiplayerReplay {
    struct TargetChange {std::uint64_t time=0;int slot=-1,target=-1;};
    std::uint64_t room=0,epoch=0,startedAt=0,duration=0,viewer=0;
    int winner=-1;
    RoundEndReason reason=RoundEndReason::Normal;
    std::vector<MultiplayerReplayTrack> tracks;
    std::vector<TargetChange> targets;
    bool complete() const;
};
struct RoomReplaySummary {
    std::uint64_t epoch=0,startedAt=0,duration=0;
    unsigned players=0;
    bool complete=false,collecting=true;
    RoundEndReason reason=RoundEndReason::Normal;
};
// Length-delimited binary envelopes preserve arbitrary UTF-8 names and legacy
// operation bytes. No gameplay or frontend dependency is needed by the server.
std::string encode_multiplayer_replay(const MultiplayerReplay& replay);
std::optional<MultiplayerReplay> decode_multiplayer_replay(std::string_view bytes);
bool is_multiplayer_replay(std::string_view bytes) noexcept;
std::uint64_t multiplayer_actor_id(std::uint64_t epoch,int slot) noexcept;
// Read only the fixed envelope header, useful when locating a local viewer
// identity without loading every player's journal.
std::optional<std::array<std::uint64_t,3>> multiplayer_replay_identity(std::string_view prefix);
std::string encode_room_replay_list(const std::vector<RoomReplaySummary>& list);
std::optional<std::vector<RoomReplaySummary>> decode_room_replay_list(std::string_view bytes);
struct JournalRecord {char kind=0;std::uint64_t time=0;std::string_view data;};
// C=config, O=original operations, B=semantic board, M=absolute motion,
// F=final statistics. The per-track real timeline is independent of rule time.
std::string journal_record(char kind,std::uint64_t time,std::string_view data);
bool take_journal_record(std::string_view& bytes,JournalRecord& record);
std::uint64_t multiplayer_digest(std::string_view bytes) noexcept;
std::string replay_hex(std::string_view bytes);
std::optional<std::string> replay_unhex(std::string_view hex,std::size_t limit);
}
#endif
