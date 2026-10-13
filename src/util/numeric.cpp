#include "tetr/util/numeric.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <limits>

namespace Util {
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
