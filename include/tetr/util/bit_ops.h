#ifndef TETR_UTIL_BIT_OPS_H
#define TETR_UTIL_BIT_OPS_H

#include <cstdint>

namespace Util {

inline int popcount32(std::uint32_t value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcount(value);
#else
    // Portable C++17 implementation; does not require CPU POPCNT support.
    value -= (value >> 1) & 0x55555555u;
    value = (value & 0x33333333u) + ((value >> 2) & 0x33333333u);
    value = (value + (value >> 4)) & 0x0f0f0f0fu;
    return static_cast<int>((value * 0x01010101u) >> 24);
#endif
}

inline int popcount64(std::uint64_t value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(value);
#else
    return popcount32(static_cast<std::uint32_t>(value)) +
        popcount32(static_cast<std::uint32_t>(value >> 32));
#endif
}

inline int trailing_zeros32(std::uint32_t value) noexcept {
    if (!value) return 32;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctz(value);
#else
    int count = 0;
    if (!(value & 0xffffu)) { count += 16; value >>= 16; }
    if (!(value & 0xffu)) { count += 8; value >>= 8; }
    if (!(value & 0xfu)) { count += 4; value >>= 4; }
    if (!(value & 0x3u)) { count += 2; value >>= 2; }
    return count + ((value & 1u) ? 0 : 1);
#endif
}

} // namespace Util

#endif
