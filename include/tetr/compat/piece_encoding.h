#ifndef TETR_COMPAT_PIECE_ENCODING_H
#define TETR_COMPAT_PIECE_ENCODING_H

#include "tetr/runtime/strong_types.h"

namespace Compat {

constexpr int to_legacy(Runtime::Rotation value) noexcept { return value.index(); }
constexpr Runtime::Rotation rotation_from_legacy(int value) noexcept {
    return Runtime::Rotation::from_index(value);
}

constexpr int to_legacy(Piece::Type value) noexcept { return value.index(); }
constexpr Piece::Type piece_type_from_legacy(int value) noexcept {
    return Piece::Type::from_index(value);
}

constexpr int to_legacy(Piece::KickIndex value) noexcept { return value.index(); }
constexpr Piece::KickIndex kick_index_from_legacy(int value) noexcept {
    return Piece::KickIndex::from_index(value);
}

constexpr int to_legacy(Runtime::RotationRecord value) noexcept {
    return value.rotated() ? value.kick().index() + 1 : 0;
}
constexpr Runtime::RotationRecord rotation_record_from_legacy(int value) noexcept {
    return value <= 0
        ? Runtime::RotationRecord::none()
        : Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(value - 1));
}

} // namespace Compat

#endif
