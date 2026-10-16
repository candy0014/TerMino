#ifndef TETR_UTIL_FILE_PATH_H
#define TETR_UTIL_FILE_PATH_H

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

namespace Util::FilePath {
namespace fs = std::filesystem;

inline bool separator(fs::path::value_type c) {
    return c == '/' || c == '\\';
}

inline std::size_t unc_root_length(const fs::path& path) {
#ifdef _WIN32
    const auto& name = path.native();
    if (name.size() < 5 || !separator(name[0]) || !separator(name[1]) || separator(name[2])) return 0;
    std::size_t serverEnd = 2;
    while (serverEnd < name.size() && !separator(name[serverEnd])) ++serverEnd;
    const auto shareBegin = serverEnd + 1;
    if (shareBegin >= name.size() || separator(name[shareBegin])) return 0;
    auto shareEnd = shareBegin;
    while (shareEnd < name.size() && !separator(name[shareEnd])) ++shareEnd;
    return shareEnd;
#else
    (void)path;
    return 0;
#endif
}

inline bool is_absolute(const fs::path& path) {
    // Some MinGW standard libraries do not recognize UNC root names.
    return path.is_absolute() || unc_root_length(path) != 0;
}

inline fs::path unc_tail(const fs::path& path, std::size_t rootLength) {
    const auto& name = path.native();
    while (rootLength < name.size() && separator(name[rootLength])) ++rootLength;
    return fs::path(name.substr(rootLength));
}

inline fs::path normalized(const fs::path& path) {
    const auto rootLength = unc_root_length(path);
    if (!rootLength) return path.lexically_normal();
    // Normalize only below the share, preserving its two leading separators.
    fs::path tail;
    for (const auto& part : unc_tail(path, rootLength)) {
        if (part == "..") tail = tail.parent_path();
        else if (part != "." && !part.empty()) tail /= part;
    }
    auto root = fs::path(path.native().substr(0, rootLength));
    return tail.empty() ? root : root / tail;
}

inline fs::path absolute(const fs::path& path, std::error_code& error) {
    if (is_absolute(path)) {
        error.clear();
        return normalized(path);
    }
    const auto result = fs::absolute(path, error);
    return error ? fs::path{} : normalized(result);
}

inline fs::path relative(const fs::path& path, const fs::path& base) {
    const auto pathRoot = unc_root_length(path), baseRoot = unc_root_length(base);
    if (!pathRoot && !baseRoot) return path.lexically_relative(base);
    if (!pathRoot || !baseRoot) return {};
    auto first = path.native().substr(0, pathRoot), second = base.native().substr(0, baseRoot);
    const auto fold = [](auto c) { return separator(c) ? '/' : std::towlower(c); };
    if (first.size() != second.size() || !std::equal(first.begin(), first.end(), second.begin(),
            [&](auto a, auto b) { return fold(a) == fold(b); })) return {};
    return unc_tail(normalized(path), pathRoot).lexically_relative(unc_tail(normalized(base), baseRoot));
}

inline std::string generic(const fs::path& path) {
    if (!unc_root_length(path)) return path.generic_u8string();
    auto name = path.u8string();
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}
} // namespace Util::FilePath

#endif
