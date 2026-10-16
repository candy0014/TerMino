#ifndef TETR_UTIL_NUMERIC_H
#define TETR_UTIL_NUMERIC_H

#include <string>
#include <string_view>
#include <optional>

namespace Util {
int digit_count(long long value);
std::string format_fixed_2(double value);
std::string format_time(double seconds);
std::optional<double> parse_time(std::string_view text);
}

#endif
