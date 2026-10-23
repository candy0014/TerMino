#include "tetr/util/text_codec.h"
#ifdef __ANDROID__
#include "../../third_party/fast_float/fast_float.h"
#endif

namespace Util::Text {

std::optional<double> parse_finite_real(std::string_view text) noexcept {
    if (text.empty()) return std::nullopt;
    double value = 0.0;
#ifdef __ANDROID__
    const auto result = fast_float::from_chars(text.data(), text.data() + text.size(), value);
#else
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
#endif
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        !std::isfinite(value)) return std::nullopt;
    return value;
}

std::vector<std::string_view> split(std::string_view text, char delimiter) {
    std::vector<std::string_view> result;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = text.find(delimiter, begin);
        if (end == std::string_view::npos) {
            result.push_back(text.substr(begin));
            return result;
        }
        result.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
}

std::size_t matching_brace(std::string_view text, std::size_t opening) noexcept {
    if (opening >= text.size() || text[opening] != '{') return std::string_view::npos;
    int depth = 1;
    for (std::size_t index = opening + 1; index < text.size(); ++index) {
        if (text[index] == '{') ++depth;
        if (text[index] == '}' && --depth == 0) return index;
    }
    return std::string_view::npos;
}

} // namespace Util::Text
