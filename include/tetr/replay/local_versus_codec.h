#ifndef TETR_REPLAY_LOCAL_VERSUS_CODEC_H
#define TETR_REPLAY_LOCAL_VERSUS_CODEC_H

#include <string>
#include <string_view>

namespace Replay {

struct LocalVersusReplayStreams {
    std::string first;
    std::string second;
};

std::string encode_local_versus_replay(
    std::string_view first,
    std::string_view second);

bool decode_local_versus_replay(
    std::string_view replay,
    LocalVersusReplayStreams& streams);

bool is_local_versus_replay(std::string_view replay) noexcept;

} // namespace Replay

#endif
