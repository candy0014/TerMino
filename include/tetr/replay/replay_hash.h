#ifndef TETR_REPLAY_REPLAY_HASH_H
#define TETR_REPLAY_REPLAY_HASH_H

#include <string>

namespace Replay {

long long legacy_replay_hash_modulus();
std::string replay_hash_suffix(const std::string& replay);

} // namespace Replay

#endif
