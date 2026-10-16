#ifndef TETR_RUNTIME_GRAVITY_PROGRESSION_H
#define TETR_RUNTIME_GRAVITY_PROGRESSION_H

#include "tetr/runtime/gravity.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Runtime {
enum class GravityProgression { Fixed, Nes, Marathon, Blitz };
inline int increasing_gravity_level(int clearedLines) {
    int remaining=std::max(0,clearedLines);
    for(int level=1;;++level) {
        const int threshold=level*2+(level+9)/10;
        if(remaining>threshold) remaining-=threshold;
        else return level;
    }
}
inline Duration increasing_gravity_interval(int level) {
    constexpr std::array<double,16> seconds{{0,1,0.643,0.404,0.249,0.15,0.088,0.0505,0.0283,0.0155,
        0.00827,0.00431,0.00219,0.00108,0.00052,0}};
    return Duration::from_seconds(seconds[std::clamp(level,1,15)]);
}
inline GravityInterval progressed_gravity(GravityProgression progression,int clearedLines,GravityInterval base,
    int linesPerLevel=10,double acceleration=1.0) {
    if(progression==GravityProgression::Blitz)
        return GravityInterval::periodic(increasing_gravity_interval(increasing_gravity_level(clearedLines)));
    if (progression==GravityProgression::Fixed || !base.enabled() || base.is_instant()) return base;
    const int level=std::max(0,clearedLines)/std::max(1,linesPerLevel);
    double ratio=1;
    if (progression==GravityProgression::Nes) {
        constexpr std::array<int,30> frames{{48,43,38,33,28,23,18,13,8,6,5,5,5,4,4,4,3,3,3,2,2,2,2,2,2,2,2,2,2,1}};
        ratio=frames[std::min(level,29)]/48.0;
    } else {
        // TGM4 Standard Marathon: internal gravity in 1/65536 rows/frame.
        constexpr std::array<int,15> gravity{{1092,1377,1767,2309,3076,4168,5748,8090,11619,17066,25401,39009,60681,99296,156038}};
        ratio=gravity.front()/static_cast<double>(gravity[std::min(level,14)]);
    }
    const double exponent=std::isfinite(acceleration) ? std::clamp(acceleration,0.01,100.0) : 1.0;
    return GravityInterval::periodic(Duration::from_seconds(base.interval().seconds()*std::pow(ratio,exponent)));
}
}
#endif
