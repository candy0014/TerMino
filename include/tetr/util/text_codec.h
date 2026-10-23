#ifndef TETR_UTIL_TEXT_CODEC_H
#define TETR_UTIL_TEXT_CODEC_H

#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace Util::Text {

std::optional<double> parse_finite_real(std::string_view text) noexcept;

template <typename Integer>
std::optional<Integer> parse_integer(std::string_view text) noexcept {
    static_assert(std::is_integral_v<Integer>, "parse_integer requires an integral type");
    if (text.empty()) return std::nullopt;
    Integer value{};
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) return std::nullopt;
    return value;
}

std::vector<std::string_view> split(std::string_view text, char delimiter);

template <std::size_t Count>
std::optional<std::array<std::string_view, Count>> split_exact(
    std::string_view text, char delimiter) noexcept {
    std::array<std::string_view, Count> fields{};
    std::size_t begin = 0;
    for (std::size_t index = 0; index < Count; ++index) {
        const std::size_t end = text.find(delimiter, begin);
        if (index + 1 == Count) {
            if (end != std::string_view::npos) return std::nullopt;
            fields[index] = text.substr(begin);
            return fields;
        }
        if (end == std::string_view::npos) return std::nullopt;
        fields[index] = text.substr(begin, end - begin);
        begin = end + 1;
    }
    return std::nullopt;
}

std::size_t matching_brace(std::string_view text, std::size_t opening) noexcept;

} // namespace Util::Text

#endif
