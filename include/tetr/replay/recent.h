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
// Only owned automatic files can be pruned; explicit saves are retained.
std::optional<std::filesystem::path> save_recent(const std::string& recording,const std::string& label);
std::vector<RecentReplay> recent_replays();
bool mark_explicitly_saved(const std::filesystem::path& source,const std::filesystem::path& destination);
}
#endif
