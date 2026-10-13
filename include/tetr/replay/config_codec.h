#ifndef TETR_REPLAY_CONFIG_CODEC_H
#define TETR_REPLAY_CONFIG_CODEC_H

#include "tetr/runtime/runtime_config.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Replay {

struct ConfigPrefixResult {
    std::size_t operationOffset = 0;
    std::optional<unsigned int> blockSeed;
    std::optional<unsigned int> garbageSeed;
    std::uint64_t undoCount = 0;
    std::uint64_t pauseCount = 0;
    std::optional<Runtime::Duration> resultGameTime;
    std::optional<Runtime::Duration> resultRealTime;
};

// Returns the historical outer opening brace plus all runtime config fields.
// Seed fields and the final outer closing brace are appended by Init.
std::string encode_config_prefix(const Runtime::RuntimeConfig& config);

// Decodes the leading {{name,value}...} block. Unknown fields are ignored so
// old/new replay files stay forward compatible. Returns false for malformed
// structure instead of indexing past the input string.
bool decode_config_prefix(
    std::string_view replay,
    Runtime::RuntimeConfig& config,
    ConfigPrefixResult& result);

} // namespace Replay

#endif
