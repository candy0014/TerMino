#ifndef TETR_RUNTIME_PIECE_CATALOG_DATA_H
#define TETR_RUNTIME_PIECE_CATALOG_DATA_H

#include <array>
#include <string>
#include <vector>
#include "tetr/runtime/game_modes.h"

namespace PieceCatalogData {

using Offset = std::array<int,2>;
using Shape = std::vector<Offset>;
using KickTable = std::array<std::vector<Offset>,12>;
struct RotationProfile {
    Runtime::RotationSystem system;
    std::array<KickTable,7> kicks;
    std::array<std::array<Offset,4>,7> origins;
    std::array<int,7> spawnRotations;
    std::array<int,7> colors;
};
const std::array<RotationProfile,8>& rotation_profiles();

const Shape (&shapes())[256][4];
const std::vector<KickTable>& kick_tables();
const std::vector<int>& shape_ids();
const std::vector<int>& color_ids();
const std::vector<int>& kick_table_ids();
const std::vector<int>& rotation_systems();
const std::vector<int>& display_row_offsets();
const std::vector<int>& display_heights();
const std::vector<int>& display_widths();
const std::vector<int>& family_indices();
const std::vector<int>& spin_checks();
const std::vector<std::string>& names();
const std::vector<int>& name_widths();

} // namespace PieceCatalogData

#endif
