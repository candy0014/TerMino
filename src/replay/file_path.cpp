#include "tetr/replay/file_path.h"

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

bool begins_with_replay_directory(const std::filesystem::path& path) {
    const auto first = path.begin();
    return first != path.end() && *first == replay_directory();
}

bool contains_parent_traversal(const std::filesystem::path& path) {
    for (const auto& component : path) {
        if (component == "..") return true;
    }
    return false;
}

std::optional<std::filesystem::path> resolve_replay_path(std::string_view text) {
    std::filesystem::path path = std::filesystem::u8path(trim_and_unquote(text));
    if (path.empty() || path.has_root_path() || contains_parent_traversal(path)) {
        return std::nullopt;
    }

    path = path.lexically_normal();
    if (begins_with_replay_directory(path)) {
        path = path.lexically_relative(replay_directory());
    }
    if (path.empty() || path == "." || contains_parent_traversal(path)) {
        return std::nullopt;
    }

    const std::filesystem::path resolved =
        (replay_directory() / path).lexically_normal();
    if (!begins_with_replay_directory(resolved)) return std::nullopt;
    return resolved;
}

} // namespace

std::filesystem::path replay_directory() {
    return std::filesystem::path("replay");
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
    return path ? path->generic_u8string() : std::string{};
}

} // namespace Replay
