#ifndef TETR_COMPAT_BOARD_ENCODING_H
#define TETR_COMPAT_BOARD_ENCODING_H

#include "tetr/runtime/board_cell.h"

namespace Compat {

constexpr int to_legacy(Runtime::BoardCell cell) noexcept {
    switch (cell.kind()) {
        case Runtime::BoardCellKind::Empty: return -1;
        case Runtime::BoardCellKind::Piece: return cell.color_id();
        case Runtime::BoardCellKind::Garbage: return 7;
        case Runtime::BoardCellKind::SolidGarbage: return 8;
        case Runtime::BoardCellKind::TimedSolid:
            return cell.timed_countdown() > 0 ? 100 + cell.timed_countdown() : 9;
        case Runtime::BoardCellKind::TimedRelease: return 100;
    }
    return -1;
}
constexpr Runtime::BoardCell board_cell_from_legacy(int value) noexcept {
    if (value < 0) return Runtime::BoardCell::empty();
    if (value == 7) return Runtime::BoardCell::garbage();
    if (value == 8) return Runtime::BoardCell::solid_garbage();
    if (value == 9) return Runtime::BoardCell::timed_solid();
    if (value == 100) return Runtime::BoardCell::timed_release();
    if (value > 100) return Runtime::BoardCell::timed_solid(value - 100);
    return Runtime::BoardCell::piece(value);
}

} // namespace Compat

#endif
