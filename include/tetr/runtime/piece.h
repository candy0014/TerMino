#ifndef TETR_RUNTIME_PIECE_H
#define TETR_RUNTIME_PIECE_H

#include "tetr/runtime/playfield.h"
#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/strong_types.h"

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace Piece {

using Offset = std::array<int, 2>;
using Shape = std::vector<Offset>;
using KickTests = std::array<std::vector<Offset>, 12>;

enum class SpinCheck {
    Immobility = 0,
    None = Immobility,
    TCorner = 1,
    ArsTCorner = 2,
    I = 3, J = 4, L = 5, O = 6, S = 7, Z = 8
};
constexpr bool t_spin_check(SpinCheck check) noexcept {
    return check==SpinCheck::TCorner || check==SpinCheck::ArsTCorner;
}

struct Metadata {
    std::string_view name;
    int nameWidth = 0;
    int colorId = 0;
    int displayRowOffset = 0;
    int displayHeight = 0;
    int displayWidth = 0;
    int familyIndex = 0;
    SpinCheck spinCheck = SpinCheck::Immobility;
    Runtime::RotationSystem rotationSystem = Runtime::RotationSystem::SrsPlus;
    bool tetrioRotation = false;
    int spawnRotation = 0;
    std::array<Offset,4> rotationOrigins{}; // (row, col), before rotation offsets.
    Offset previewOffset{}; // Keep canonical matrix origins out of preview layout.
};

struct CatalogEntry {
    Type type = Type::none();
    int shapeIndex = 0;
    int kickTableIndex = 0;
    Metadata metadata{};
    std::array<Shape, 4> shapes{};
    KickTests kickTests{};
};

class Definition {
public:
    Definition() = default;
    Definition(Type type, int colorId, std::array<Shape, 4> shapes, KickTests kicks)
        : shapes_(std::move(shapes)), kicks_(std::move(kicks)), type_(type), colorId_(colorId) {}

    Type type() const noexcept { return type_; }
    int color_id() const noexcept { return colorId_; }
    const Shape& cells(Runtime::Rotation rotation) const { return shapes_[rotation.index()]; }
    const KickTests& kick_tests() const noexcept { return kicks_; }

    bool can_place(Pose pose, const Runtime::Playfield& field) const;
    bool can_place(Runtime::CellPosition position, Runtime::Rotation rotation, const Runtime::Playfield& field) const {
        return can_place(Pose{position, rotation}, field);
    }
    bool try_rotate(Pose& pose, Runtime::Rotation targetRotation,
                    const Runtime::Playfield& field, KickIndex& kickIndex) const;

private:
    std::array<Shape, 4> shapes_{};
    KickTests kicks_{};
    Type type_ = Type::none();
    int colorId_ = 0;
};

const std::vector<CatalogEntry>& catalog();
const CatalogEntry& catalog_entry(Type type);
Type fool_display_type(Type type);
Definition get_definition(Type type);
Metadata metadata(Type type);
// Initial pose before input, gravity and Clutch. Event pieces can be fitted
// inside narrow Snowball walls without consulting occupied board cells.
Pose spawn_pose(int width, Type type = Type::none());
Pose fit_spawn_inside_walls(Pose pose, const Shape& cells, int width);
Shape preview_cells(Type type);
// April Fools includes the normal seven pieces and event floors 1 through
// foolFloorLimit, inclusive. Omitting the limit enumerates the full catalog.
std::vector<Type> types_for_rotation_system(Runtime::RotationSystem system, bool legacy = false, int foolFloorLimit = 10,
    bool legacyEvents = false);
std::vector<Type> april_fools_piece_types(bool legacy = false);
Type april_fools_piece(int familyIndex, bool legacy = false);
Type canonical_event_piece(Type type);
constexpr bool event_rotation_system(Runtime::RotationSystem system) noexcept {
    return system==Runtime::RotationSystem::Pentomino || system==Runtime::RotationSystem::Hexomino ||
        system==Runtime::RotationSystem::AprilFools2026;
}
constexpr int standard_tetromino_count() noexcept { return 7; }

} // namespace Piece

#endif
