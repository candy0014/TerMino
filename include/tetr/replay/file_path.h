#ifndef TETR_REPLAY_FILE_PATH_H
#define TETR_REPLAY_FILE_PATH_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Replay {

// Bare replay names use this directory; "demo.txt", ".tetr/replay/demo.txt"
// and the legacy "replay/demo.txt" are equivalent. Absolute paths retain
// their location; ./ and ../ references are relative to the project folder.
std::filesystem::path replay_directory();
std::optional<std::filesystem::path> replay_path_for_loading(std::string_view text);

// Interactive and environment-driven saves use the same path rules as loading.
std::optional<std::filesystem::path> replay_path_for_saving(std::string_view text);

// Stored references are project-relative, using ./ when needed to distinguish
// them from bare names. Absolute paths remain only when no relative path exists
// (e.g. a different Windows drive). Empty input remains empty.
std::string replay_setting_path(std::string_view text);

} // namespace Replay

#endif
