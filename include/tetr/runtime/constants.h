#ifndef TETR_RUNTIME_CONSTANTS_H
#define TETR_RUNTIME_CONSTANTS_H

namespace Runtime {
inline constexpr int kDefaultBoardMargin = 30;
inline constexpr int kHiddenRowCapacity = 100;
inline constexpr int kDisplayHiddenRows = 8;
inline constexpr unsigned int kMultiplayerBlockSeedSalt = 114514u;
inline constexpr unsigned int kAttackRoundingSeedSalt = 0xA77AC4u;
}

#endif
