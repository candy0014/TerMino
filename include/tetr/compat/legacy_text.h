#ifndef TETR_COMPAT_LEGACY_TEXT_H
#define TETR_COMPAT_LEGACY_TEXT_H

#include <climits>
#include <limits>
#include <string_view>

namespace Compat::LegacyText {

// Historical permissive numeric parsing used by user_config.txt, Settings text
// editing and Replay text. Keep this behavior at explicit compatibility
// boundaries; normal typed/runtime code must use strict parsers instead.
inline long long parse_int(std::string_view text) noexcept {
    // Preserve the historical permissive parser (ignore non-digits and accept
    // a leading '-' before the first non-zero digit), but saturate instead of
    // invoking signed-overflow UB on malformed or extremely long input.
    unsigned long long magnitude = 0;
    int sign = 1;
    for (char ch : text) {
        if (ch == '-' && magnitude == 0 && sign == 1) {
            sign = -1;
            continue;
        }
        if (ch < '0' || ch > '9') continue;
        const unsigned digit = static_cast<unsigned>(ch - '0');
        const unsigned long long limit = sign < 0
            ? static_cast<unsigned long long>(LLONG_MAX) + 1ULL
            : static_cast<unsigned long long>(LLONG_MAX);
        if (magnitude > (limit - digit) / 10ULL) {
            magnitude = limit;
            continue;
        }
        magnitude = magnitude * 10ULL + digit;
    }
    if (sign < 0) {
        const unsigned long long minMagnitude = static_cast<unsigned long long>(LLONG_MAX) + 1ULL;
        if (magnitude >= minMagnitude) return LLONG_MIN;
        return -static_cast<long long>(magnitude);
    }
    return static_cast<long long>(magnitude);
}

inline double parse_real(std::string_view text) noexcept {
    double value = 0.0;
    double weight = 1.0;
    bool decimal = false;
    int sign = 1;
    for (char ch : text) {
        if ('0' <= ch && ch <= '9') {
            if (!decimal) value = value * 10.0 + static_cast<double>(ch - '0');
            else {
                weight /= 10.0;
                value += weight * static_cast<double>(ch - '0');
            }
        }
        if (ch == '-' && value == 0.0 && sign == 1 && !decimal) sign = -1;
        if (ch == '.') decimal = true;
    }
    return value * static_cast<double>(sign);
}

} // namespace Compat::LegacyText

#endif
