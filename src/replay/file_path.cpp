#include "tetr/replay/file_path.h"
#include "tetr/util/file_path.h"

#include <cctype>

namespace Replay {
namespace {

std::string trim_and_unquote(std::string_view text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    if (last - first >= 2 &&
        ((text[first] == '\'' && text[last - 1] == '\'') ||
         (text[first] == '"' && text[last - 1] == '"'))) {
        ++first;
        --last;
    }
    return std::string(text.substr(first, last - first));
}

bool begins_with_directory(const std::filesystem::path& path,
                           const std::filesystem::path& directory) {
    auto component = path.begin();
    for (const auto& prefix : directory) {
        if (component == path.end() || *component != prefix) return false;
        ++component;
    }
    return true;
}

std::optional<std::filesystem::path> resolve_replay_path(std::string_view text) {
    std::filesystem::path path = std::filesystem::u8path(trim_and_unquote(text));
    if (path.empty() || (path.has_root_path() && !Util::FilePath::is_absolute(path))) return std::nullopt;
    const bool projectRelative = *path.begin() == "." || *path.begin() == ".." || *path.begin() == ".tetr";
    if (!Util::FilePath::is_absolute(path) && !projectRelative) {
        // Bare names use the default directory; explicit ./ and ../ paths
        // are relative to the project. Absolute paths retain their location.
        if (begins_with_directory(path, "replay")) path = path.lexically_relative("replay");
        path = replay_directory() / path;
    }
    path = Util::FilePath::normalized(path);
    if (const auto root = Util::FilePath::unc_root_length(path); root && root == path.native().size())
        return std::nullopt;
    if (path.filename().empty() || path.filename() == "." || path.filename() == ".." || path == replay_directory())
        return std::nullopt;
    return path;
}

} // namespace

std::filesystem::path replay_directory() {
    return std::filesystem::path(".tetr") / "replay";
}

std::optional<std::filesystem::path> replay_path_for_loading(std::string_view text) {
    return resolve_replay_path(text);
}

std::optional<std::filesystem::path> replay_path_for_saving(std::string_view text) {
    return resolve_replay_path(text);
}

std::string replay_setting_path(std::string_view text) {
    const std::string normalized = trim_and_unquote(text);
    if (normalized.empty()) return {};
    const auto path = replay_path_for_loading(normalized);
    if (!path) return {};
    auto relative = *path;
    if (Util::FilePath::is_absolute(relative)) {
        std::error_code error;const auto root = std::filesystem::current_path(error);
        if (error) return Util::FilePath::generic(relative);
        const auto rebased = Util::FilePath::relative(relative, root);
        // Windows paths on a different drive cannot be made relative.
        if (rebased.empty()) return Util::FilePath::generic(relative);
        relative = rebased;
    }
    if (begins_with_directory(relative, replay_directory()) || *relative.begin() == "..")
        return Util::FilePath::generic(relative);
    // Keep project-relative references distinct from bare replay filenames.
    return "./" + Util::FilePath::generic(relative);
}

} // namespace Replay
