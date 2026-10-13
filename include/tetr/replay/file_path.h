#ifndef TETR_REPLAY_FILE_PATH_H
#define TETR_REPLAY_FILE_PATH_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Replay {

// User-facing replay names are rooted at this directory.  Both loading and
// saving reject absolute paths and parent traversal; "demo.txt" and
// "replay/demo.txt" are equivalent.
std::filesystem::path replay_directory();
std::optional<std::filesystem::path> replay_path_for_loading(std::string_view text);

// Interactive and environment-driven saves are deliberately confined to the
// replay directory.  Absolute paths and parent traversal are rejected.
std::optional<std::filesystem::path> replay_path_for_saving(std::string_view text);

// Value stored by the Settings UI.  Empty input remains empty; relative names
// are made explicit so the menu shows where Replay mode will read from.
std::string replay_setting_path(std::string_view text);

} // namespace Replay

#endif
