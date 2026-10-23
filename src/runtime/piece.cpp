#include "tetr/runtime/piece.h"

#include "piece_catalog_data.h"
#include "piece_event_geometry.h"

#include <stdexcept>
#include <algorithm>
#include <utility>

namespace Piece {
namespace {
int rotation_transition(Runtime::Rotation fromRotation, Runtime::Rotation toRotation) {
    const int from = fromRotation.index();
    const int to = toRotation.index();
    if(from==0&&to==1) return 0;
    if(from==1&&to==0) return 1;
    if(from==1&&to==2) return 2;
    if(from==2&&to==1) return 3;
    if(from==2&&to==3) return 4;
    if(from==3&&to==2) return 5;
    if(from==3&&to==0) return 6;
    if(from==0&&to==3) return 7;
    if(from==0&&to==2) return 8;
    if(from==2&&to==0) return 9;
    if(from==1&&to==3) return 10;
    if(from==3&&to==1) return 11;
    return 8;
}

std::vector<CatalogEntry> build_catalog() {
    const auto& names = PieceCatalogData::names();
    const auto& shapeIds = PieceCatalogData::shape_ids();
    const auto& colorIds = PieceCatalogData::color_ids();
    const auto& kickTableIds = PieceCatalogData::kick_table_ids();
    const auto& rotationSystems = PieceCatalogData::rotation_systems();
    const auto& displayRowOffsets = PieceCatalogData::display_row_offsets();
    const auto& displayHeights = PieceCatalogData::display_heights();
    const auto& displayWidths = PieceCatalogData::display_widths();
    const auto& familyIndices = PieceCatalogData::family_indices();
    const auto& spinChecks = PieceCatalogData::spin_checks();
    const auto& nameWidths = PieceCatalogData::name_widths();
    const auto& shapes = PieceCatalogData::shapes();
    const auto& kickTables = PieceCatalogData::kick_tables();
    const std::size_t size = names.size();
    std::vector<CatalogEntry> result;
    result.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        const int shapeIndex = shapeIds[i];
        const int kickTableIndex = kickTableIds[i];
        CatalogEntry entry;
        entry.type = Type::from_index(static_cast<int>(i));
        entry.shapeIndex = shapeIndex;
        entry.kickTableIndex = kickTableIndex;
        entry.metadata = Metadata{
            names[i],
            nameWidths[i],
            colorIds[i],
            displayRowOffsets[i],
            displayHeights[i],
            displayWidths[i],
            familyIndices[i],
            static_cast<SpinCheck>(spinChecks[i]),
            Runtime::rotation_system_from_index(rotationSystems[i])};
        for (int rotation = 0; rotation < 4; ++rotation) {
            entry.shapes[rotation] = shapes[shapeIndex][rotation];
        }
        entry.kickTests = kickTables[kickTableIndex];
        result.push_back(std::move(entry));
    }
    // Keep the original 173 identifiers intact for existing recordings.
    // New games use a separate definition for every standard rotation system.
    for (const auto& profile : PieceCatalogData::rotation_profiles()) {
        for (int family = 0; family < standard_tetromino_count(); ++family) {
            CatalogEntry entry = result[family];
            entry.type = Type::from_index(static_cast<int>(result.size()));
            entry.kickTableIndex = -1;
            entry.kickTests = profile.kicks[family];
            entry.metadata.rotationSystem = profile.system;
            entry.metadata.tetrioRotation = true;
            entry.metadata.rotationOrigins = profile.origins[family];
            entry.metadata.spawnRotation = profile.spawnRotations[family];
            entry.metadata.colorId = profile.colors[family];
            result.push_back(std::move(entry));
        }
    }
    // Append canonical event matrices; never change geometry behind an existing
    // map/replay/network piece identifier.
    for(int legacy=21;legacy<173;++legacy) {
        if(!event_rotation_system(result[legacy].metadata.rotationSystem))continue;
        CatalogEntry entry=result[legacy];
        entry.type=Type::from_index(static_cast<int>(result.size()));
        entry.metadata.tetrioRotation=true;
        for(const auto& correction:EventGeometry::corrections)if(correction.legacy==legacy) {
            for(int r=0;r<4;++r) {
                entry.shapes[r]=result[correction.source].shapes[correction.rotations[r]];
                for(auto& cell:entry.shapes[r]) {
                    cell[0]+=correction.translations[r][0];cell[1]+=correction.translations[r][1];
                }
            }
        }
        const auto minimum=[](const Shape& shape,int axis) {
            int value=shape.front()[axis];for(const auto& cell:shape)value=std::min(value,cell[axis]);return value;
        };
        for(int axis=0;axis<2;++axis)
            entry.metadata.previewOffset[axis]=minimum(result[legacy].shapes[0],axis)-minimum(entry.shapes[0],axis);
        result.push_back(std::move(entry));
    }
    return result;
}
} // namespace

const std::vector<CatalogEntry>& catalog() {
    static const std::vector<CatalogEntry> entries = build_catalog();
    return entries;
}

const CatalogEntry& catalog_entry(Type type) {
    const int index = type.index();
    const auto& entries = catalog();
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        throw std::out_of_range("piece type outside catalog");
    }
    return entries[static_cast<std::size_t>(index)];
}

bool Definition::can_place(Pose pose, const Runtime::Playfield& field) const {
    for (const auto& offset : cells(pose.rotation)) {
        const int testRow = pose.position.row + offset[0];
        const int testCol = pose.position.col + offset[1];
        if (!field.contains(testRow, testCol)) return false;
        if (field.cell(testRow, testCol).occupied()) return false;
    }
    return true;
}

bool Definition::try_rotate(Pose& pose, Runtime::Rotation targetRotation,
                            const Runtime::Playfield& field, KickIndex& kickIndex) const {
    const int operation = rotation_transition(pose.rotation, targetRotation);
    const Metadata pieceMetadata = metadata(type());
    if (!pieceMetadata.tetrioRotation && pieceMetadata.rotationSystem == Runtime::RotationSystem::Ars &&
        (pieceMetadata.familyIndex == 1 || pieceMetadata.familyIndex == 2 || pieceMetadata.familyIndex == 5)) {
        for (int testRow = pose.position.row - 1; testRow <= pose.position.row + 1; ++testRow) {
            for (int testCol = pose.position.col - 1; testCol <= pose.position.col + 1; ++testCol) {
                if (!field.contains(testRow, testCol) || field.cell(testRow, testCol).occupied()) {
                    if (testCol == pose.position.col) return false;
                    testRow = pose.position.row + 1;
                    break;
                }
            }
        }
    }

    int index = 0;
    for (const auto& kick : kicks_[operation]) {
        Pose candidate = pose;
        candidate.position.row -= kick[1];
        candidate.position.col += kick[0];
        candidate.rotation = targetRotation;
        if (can_place(candidate, field)) {
            if (index > 0 && pieceMetadata.tetrioRotation &&
                pieceMetadata.rotationSystem == Runtime::RotationSystem::Ars &&
                (pieceMetadata.familyIndex == 1 || pieceMetadata.familyIndex == 2 || pieceMetadata.familyIndex == 5)) {
                const auto& offset = pieceMetadata.rotationOrigins[pose.rotation.index()];
                bool forbidden = false, found = false;
                for (int row = -1; row <= 1 && !found; ++row) {
                    for (int col = -1; col <= 1; ++col) {
                        const int y = pose.position.row - offset[0] + row;
                        const int x = pose.position.col - offset[1] + col;
                        if (!field.contains(y,x) || field.cell(y,x).occupied()) {
                            forbidden = col == 0;
                            found = true;
                            break;
                        }
                    }
                }
                if (forbidden) { ++index; continue; }
            }
            pose = candidate;
            kickIndex = KickIndex::from_index(index);
            return true;
        }
        ++index;
    }
    return false;
}

Type fool_display_type(Type type) {
    const auto& entry = catalog_entry(type);
    if (entry.metadata.rotationSystem == Runtime::RotationSystem::AprilFools2026) {
        static constexpr int mapped[7] = {6, 2, 3, 4, 0, 1, 5};
        const int familyClass = entry.metadata.familyIndex % standard_tetromino_count();
        return Type::from_index(mapped[familyClass]);
    }
    return type;
}

Definition get_definition(Type type) {
    const auto& entry = catalog_entry(type);
    return Definition{entry.type, entry.metadata.colorId, entry.shapes, entry.kickTests};
}

Metadata metadata(Type type) { return catalog_entry(type).metadata; }

Pose spawn_pose(int width, Type type) {
    Pose pose{{-2,(width-1)/2},Runtime::Rotation{}};
    if (type.valid()) {
        const auto info=metadata(type);
        pose.rotation=Runtime::Rotation::from_index(info.spawnRotation);
        const auto& origin=info.rotationOrigins[info.spawnRotation];
        pose=pose.translated(origin[0],origin[1]);
    }
    return pose;
}

Pose fit_spawn_inside_walls(Pose pose, const Shape& cells, int width) {
    if (cells.empty()) return pose;
    int left=cells.front()[1],right=left;
    for (const auto& offset:cells) {
        left=std::min(left,offset[1]); right=std::max(right,offset[1]);
    }
    if (right-left+1<=width)
        pose.position.col=std::clamp(pose.position.col,-left,width-1-right);
    return pose;
}

Shape preview_cells(Type type) {
    const auto& entry = catalog_entry(type);
    Shape cells = entry.shapes[entry.metadata.spawnRotation];
    const auto minimumRow = [](const Shape& shape) {
        int minimum = shape.front()[0];
        for (const auto& cell:shape) minimum = std::min(minimum,cell[0]);
        return minimum;
    };
    const int shift = minimumRow(entry.shapes[0]) - minimumRow(cells);
    for (auto& cell:cells) {
        cell[0] += shift+entry.metadata.previewOffset[0];cell[1]+=entry.metadata.previewOffset[1];
    }
    return cells;
}

std::vector<Type> april_fools_piece_types(bool legacy) {
    std::vector<Type> result;
    for (const auto& entry : catalog()) {
        if (entry.metadata.rotationSystem == Runtime::RotationSystem::AprilFools2026 &&
            entry.metadata.tetrioRotation!=legacy) result.push_back(entry.type);
    }
    return result;
}

Type april_fools_piece(int familyIndex, bool legacy) {
    for (const auto& entry : catalog()) {
        if (entry.metadata.rotationSystem == Runtime::RotationSystem::AprilFools2026 &&
            entry.metadata.familyIndex == familyIndex && entry.metadata.tetrioRotation!=legacy) return entry.type;
    }
    throw std::out_of_range("April Fools piece family outside catalog");
}

std::vector<Type> types_for_rotation_system(Runtime::RotationSystem system, bool legacy, int foolFloorLimit, bool legacyEvents) {
    std::vector<Type> result;
    const int foolFamilyLimit=std::clamp(foolFloorLimit,1,10)*standard_tetromino_count();
    if (system == Runtime::RotationSystem::AprilFools2026) {
        for (int i = 0; i < standard_tetromino_count(); ++i) result.push_back(Type::from_index(i));
    }
    for (const auto& entry : catalog()) {
        if (entry.metadata.rotationSystem == system &&
            (system != Runtime::RotationSystem::AprilFools2026 || entry.metadata.familyIndex < foolFamilyLimit) &&
            (!event_rotation_system(system) || entry.metadata.tetrioRotation!=legacyEvents) &&
            (!Runtime::standard_rotation_system(system) || entry.metadata.tetrioRotation != legacy))
            result.push_back(entry.type);
    }
    return result;
}

Type canonical_event_piece(Type type) {
    // The three appended event ranges follow the immutable legacy ranges.
    const int id=type.index();
    if(id>=21 && id<39)return Type::from_index(229+id-21);
    if(id>=43 && id<173)return Type::from_index(247+id-43);
    return type;
}

} // namespace Piece
