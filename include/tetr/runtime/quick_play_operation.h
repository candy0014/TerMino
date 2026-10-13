#ifndef TETR_RUNTIME_QUICK_PLAY_OPERATION_H
#define TETR_RUNTIME_QUICK_PLAY_OPERATION_H

#include "tetr/runtime/strong_types.h"

namespace Runtime {

// Semantic Quick Play clear operation used by the simulation. Historical
// packed integers are intentionally kept out of Runtime state and are encoded
// only by explicit legacy/codec helpers at the boundaries.
struct QuickPlayClearOperation {
    Piece::Type pieceType = Piece::Type::from_index(0);
    int linesCleared = 0;
    bool spin = false;
    bool mini = false;

    friend constexpr bool operator==(const QuickPlayClearOperation& lhs,
                                     const QuickPlayClearOperation& rhs) noexcept {
        return lhs.pieceType == rhs.pieceType &&
               lhs.linesCleared == rhs.linesCleared &&
               lhs.spin == rhs.spin && lhs.mini == rhs.mini;
    }
    friend constexpr bool operator!=(const QuickPlayClearOperation& lhs,
                                     const QuickPlayClearOperation& rhs) noexcept {
        return !(lhs == rhs);
    }
};

// Canonical comparison key for the historical Extended All-Spin history.
// The old packed representation had overlapping piece/line bits.  These
// named fields preserve the exact equivalence classes without retaining an
// opaque packed integer in gameplay state.
struct ExtendedAllSpinSignature {
    int pieceClass = 0;       // historical normalized bits 0..6
    int lineClass = 0;        // historical normalized bits 7..9
    bool spinClass = false;   // historical normalized bit 10
    bool miniClass = false;   // historical normalized bit 11
    int upperPieceClass = 0;  // historical normalized bits >= 12

    friend constexpr bool operator==(const ExtendedAllSpinSignature& lhs,
                                     const ExtendedAllSpinSignature& rhs) noexcept {
        return lhs.pieceClass == rhs.pieceClass &&
               lhs.lineClass == rhs.lineClass &&
               lhs.spinClass == rhs.spinClass &&
               lhs.miniClass == rhs.miniClass &&
               lhs.upperPieceClass == rhs.upperPieceClass;
    }
    friend constexpr bool operator!=(const ExtendedAllSpinSignature& lhs,
                                     const ExtendedAllSpinSignature& rhs) noexcept {
        return !(lhs == rhs);
    }
};

} // namespace Runtime

#endif
