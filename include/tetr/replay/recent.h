#ifndef TETR_REPLAY_RECENT_H
#define TETR_REPLAY_RECENT_H
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace Replay {
inline constexpr std::size_t kRecentReplayLimit=30;
struct RecentReplay {std::filesystem::path path;std::string label;};
// Retain the last 30 rounds. Only unchanged files managed by this cache are
// pruned; playback Save As creates a separate, unmanaged copy.
std::optional<std::filesystem::path> save_recent(const std::string& recording,const std::string& label);
// Replace a managed cache entry transactionally, leaving an externally edited
// source untouched. Commit a new file/index before pruning the original.
std::optional<std::filesystem::path> replace_recent(const std::filesystem::path& source,
    const std::string& recording,const std::string& label);
std::vector<RecentReplay> recent_replays();
// Explicit deletion removes indexed files, including renamed/protected rounds.
// Unindexed files (such as playback Save As copies) are never touched. Failed
// deletions remain indexed; clear_recent returns false on any failure.
bool delete_recent(const std::filesystem::path& source);
bool clear_recent();
// After saving a finished round under a new name, replace its cache reference
// and remove the old automatic file. Files outside replay_directory are kept.
bool rename_recent(const std::filesystem::path& source,const std::filesystem::path& destination);
// Compatibility for callers that explicitly protect an existing cache file.
bool mark_explicitly_saved(const std::filesystem::path& source,const std::filesystem::path& destination);
}
#endif
