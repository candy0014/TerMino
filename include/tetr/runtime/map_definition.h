#ifndef TETR_RUNTIME_MAP_DEFINITION_H
#define TETR_RUNTIME_MAP_DEFINITION_H

#include "tetr/runtime/board_cell.h"
#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/strong_types.h"
#include <string>
#include <string_view>
#include <vector>

namespace Runtime {
struct RuntimeConfig;
enum class MapGenerator { RotationSystem, CustomSequence };
enum class MapGoal { Disabled, ClearInitial, AllClear };
struct MapDefinition {
    int id=0;
    std::string name;
    int width=10,height=20;
    // Wiki symbols, top to bottom: space, G, I, J, L, O, S, T, Z.
    std::vector<std::string> rows=std::vector<std::string>(20,std::string(10,' '));
    MapGenerator generator=MapGenerator::RotationSystem;
    RotationSystem rotationSystem=RotationSystem::SrsPlus;
    int foolFloor=1;
    MapGoal goal=MapGoal::Disabled;
    std::vector<Piece::Type> sequence;
    bool operator==(const MapDefinition& other) const;
    bool operator!=(const MapDefinition& other) const { return !(*this==other); }
};
bool valid_map(const MapDefinition& map);
BoardCell map_cell(char symbol);
char map_symbol(BoardCell cell);
// Resizing keeps the bottom-left corner and fills newly exposed cells empty.
void resize_map(MapDefinition& map,int width,int height);
std::string encode_map(const MapDefinition& map);
bool decode_map(std::string_view text,MapDefinition& map);
// Native replay prefixes embed this hex-encoded representation, independently
// of the user's map collection or later edits to it.
std::string encode_map_hex(const MapDefinition& map);
bool decode_map_hex(std::string_view text,MapDefinition& map);
void apply_map_config(RuntimeConfig& config,const MapDefinition& map);
// A construction target owns only size and occupancy, independent of colors,
// generation, rotation, sequence and the map's original completion goal.
MapDefinition construction_map_target(const MapDefinition& map);
} // namespace Runtime
#endif
