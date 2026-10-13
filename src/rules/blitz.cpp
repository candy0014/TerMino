#include "tetr/rules/blitz.h"

#include <algorithm>

namespace Rules {

int blitz_level_from_lines(int clearedLines) {
    int remaining=clearedLines;
    for(int level=1;;++level){
        const int threshold=level*2+(level+9)/10;
        if(remaining>threshold) remaining-=threshold;
        else return level;
    }
}

Runtime::Duration blitz_gravity_for_level(int level) {
    static constexpr double kSpeedTable[20]={
        0,1,0.643,0.404,0.249,0.15,0.088,0.0505,0.0283,0.0155,
        0.00827,0.00431,0.00219,0.00108,0.00052,0,0,0,0,0
    };
    return Runtime::Duration::from_seconds(kSpeedTable[std::min(level,15)]);
}

} // namespace Rules
