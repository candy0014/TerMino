#include "tetr/util/text_codec.h"

namespace Util::Text {

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
