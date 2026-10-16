#ifndef TETR_RUNTIME_CHALLENGE_PUZZLES_H
#define TETR_RUNTIME_CHALLENGE_PUZZLES_H
#include "tetr/runtime/map_definition.h"
#include "tetr/runtime/piece.h"
#include <algorithm>
#include <cstdint>
namespace Runtime {
// Version 1: three small, independently solvable O/I/T recognition puzzles.
// Rows and fixed pieces are embedded in native recordings through initialMap.
inline MapDefinition challenge_puzzle(int stage) {
    MapDefinition map;map.name="PC training v1 #"+std::to_string(stage+1);map.goal=MapGoal::AllClear;
    map.generator=MapGenerator::CustomSequence;map.sequence={Piece::Type::from_index(stage%3==0?3:stage%3==1?0:5)};
    const auto piece=Piece::get_definition(map.sequence.front());const auto cells=piece.cells(Rotation(stage%3==2?2:0));
    int top=100,bottom=-100;for(const auto& cell:cells){top=std::min(top,cell[0]);bottom=std::max(bottom,cell[0]);}
    const int originRow=map.height-1-bottom,originCol=stage%3==0?2:stage%3==1?4:5;
    for(int row=originRow+top;row<map.height;++row)map.rows[row]=std::string(map.width,'G');
    for(const auto& cell:cells)map.rows[originRow+cell[0]][originCol+cell[1]]=' ';
    return map;
}
inline std::string challenge_training_id(bool series) {
    std::uint64_t hash=14695981039346656037ull;
    for(int stage=0;stage<(series?3:1);++stage) {
        auto map=challenge_puzzle(stage);map.name.clear();
        for(unsigned char c:encode_map(map)){hash^=c;hash*=1099511628211ull;}
    }
    return std::string(series?"pc-training-series-v1-":"pc-training-puzzle-v1-")+std::to_string(hash);
}
}
#endif
