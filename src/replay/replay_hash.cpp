#include "tetr/replay/replay_hash.h"

namespace Replay {

long long legacy_replay_hash_modulus() {
    // Intentionally preserve the historical floating-point expression. At
    // this magnitude, 1e16 + 2137 rounds to 10000000000002136 before the
    // conversion to long long. Existing replay files depend on that value.
    long long mod=1e16+2137;
    return mod;
}

std::string replay_hash_suffix(const std::string& replay) {
    const long long mod=legacy_replay_hash_modulus();
    const long long base=233;
    long long hash=0;
    for(std::size_t i=0;i<replay.size()&&replay[i];++i) hash=(hash*base+static_cast<int>(replay[i]))%mod;
    std::string encoded=std::to_string(hash);
    while(encoded.length()<17) encoded="0"+encoded;
    return encoded;
}

} // namespace Replay
