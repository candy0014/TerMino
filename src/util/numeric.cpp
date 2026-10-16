#include "tetr/util/numeric.h"
#include "tetr/util/text_codec.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <limits>

namespace Util {
std::string format_time(double seconds) {
    if (!std::isfinite(seconds)) return "--:--.---";
    const long double millis=std::round(std::fabs(static_cast<long double>(seconds))*1000);
    const auto minutes=std::floor(millis/60000);
    std::ostringstream out;
    if(seconds<0 && millis>0) out<<'-';
    out<<std::setfill('0')<<std::fixed<<std::setprecision(0)<<std::setw(2)<<minutes<<':'
       <<std::setprecision(3)<<std::setw(6)<<std::fmod(millis,60000)/1000;
    return out.str();
}
std::optional<double> parse_time(std::string_view text) {
    const auto colon=text.find(':');
    if(colon==std::string_view::npos) return Text::parse_finite_real(text);
    const auto minutes=Text::parse_integer<unsigned long long>(text.substr(0,colon));
    const auto seconds=Text::parse_finite_real(text.substr(colon+1));
    if(!minutes || !seconds || *seconds<0) return std::nullopt;
    const double total=static_cast<double>(*minutes)*60+*seconds;
    return std::isfinite(total)?std::optional<double>{total}:std::nullopt;
}
int digit_count(long long value){
    if(value==0) return 1;
    int count=value<0 ? 1 : 0;
    const unsigned long long magnitude = value < 0
        ? 0ULL - static_cast<unsigned long long>(value)
        : static_cast<unsigned long long>(value);
    unsigned long long remaining=magnitude;
    while(remaining){ ++count; remaining/=10ULL; }
    return count;
}

std::string format_fixed_2(double value){
    if(std::fabs(value)<=1e-8) return "0.00";
    if(!std::isfinite(value)) return value < 0 ? "-inf" : "inf";
    // Keep the historical truncation-to-two-decimals behavior without
    // converting an arbitrary double to int (undefined when out of range).
    const double truncated = std::fabs(value) <= std::numeric_limits<double>::max()/100.0
        ? std::trunc(value*100.0)/100.0
        : value;
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << truncated;
    return out.str();
}

}
