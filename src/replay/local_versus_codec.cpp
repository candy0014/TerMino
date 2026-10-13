#include "tetr/replay/local_versus_codec.h"

#include "tetr/util/text_codec.h"

#include <limits>

namespace Replay {
namespace {

constexpr std::string_view kMagic = "TETR_LOCAL_VERSUS_REPLAY_1:";

bool parse_size(std::string_view replay, std::size_t& position, std::size_t& value) {
    const std::size_t separator = replay.find(':', position);
    if (separator == std::string_view::npos || separator == position) return false;
    const auto parsed = Util::Text::parse_integer<unsigned long long>(
        replay.substr(position, separator - position));
    if (!parsed || *parsed > std::numeric_limits<std::size_t>::max()) return false;
    value = static_cast<std::size_t>(*parsed);
    position = separator + 1;
    return true;
}

} // namespace

std::string encode_local_versus_replay(
    std::string_view first,
    std::string_view second) {
    std::string result(kMagic);
    result += std::to_string(first.size());
    result += ':';
    result += std::to_string(second.size());
    result += ':';
    result.append(first.data(), first.size());
    result.append(second.data(), second.size());
    return result;
}

bool decode_local_versus_replay(
    std::string_view replay,
    LocalVersusReplayStreams& streams) {
    if (!is_local_versus_replay(replay)) return false;
    std::size_t position = kMagic.size();
    std::size_t firstSize = 0;
    std::size_t secondSize = 0;
    if (!parse_size(replay, position, firstSize) ||
        !parse_size(replay, position, secondSize) ||
        firstSize > replay.size() - position ||
        secondSize != replay.size() - position - firstSize) {
        return false;
    }
    streams.first.assign(replay.substr(position, firstSize));
    streams.second.assign(replay.substr(position + firstSize, secondSize));
    return true;
}

bool is_local_versus_replay(std::string_view replay) noexcept {
    return replay.substr(0, kMagic.size()) == kMagic;
}

} // namespace Replay
