#ifndef TETR_RUNTIME_CONSTRUCTION_H
#define TETR_RUNTIME_CONSTRUCTION_H
#include "tetr/runtime/runtime_config.h"
#include "tetr/runtime/playfield.h"
#include "tetr/runtime/constants.h"
#include <algorithm>
namespace Runtime {
// Display hints never occupy the playfield or participate in collisions.
inline bool construction_cell(const RuntimeConfig& config,const Playfield& field,int row,int column) {
    if(row<0 || row>=field.height() || column<0 || column>=field.width())return false;
    const int layer=field.height()-1-row,width=field.width();
    switch(config.challengeRules) {
    case ChallengeRules::ConstructSecret: {
        const int layers=std::min(field.height()-1,2*width-1);
        const auto hole=[width](int level){return level<width?level:2*width-2-level;};
        return layer<layers?column!=hole(layer):layer==layers && layers>0 && column==hole(layers-1);
    }
    case ChallengeRules::ConstructCheckerboard:return (column+layer)%2==0;
    case ChallengeRules::ConstructMap:
        return config.initialMap && row<config.initialMap->height && column<config.initialMap->width &&
            map_cell(config.initialMap->rows[row][column]).occupied();
    case ChallengeRules::ConstructSymmetry:
        return !field.cell(row,column).occupied() && field.cell(row,width-1-column).occupied();
    default:return false;
    }
}
inline bool construction_map_matches(const RuntimeConfig& config,const Playfield& field) {
    if(config.challengeRules!=ChallengeRules::ConstructMap || !config.initialMap ||
       config.initialMap->width!=field.width() || config.initialMap->height!=field.height())return false;
    for(int row=-kHiddenRowCapacity;row<field.height();++row)for(int col=0;col<field.width();++col)
        if(field.cell(row,col).occupied()!=construction_cell(config,field,row,col))return false;
    return true;
}
}
#endif
