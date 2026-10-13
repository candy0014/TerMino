#ifndef TETR_RULES_OPENER_PHASE_H
#define TETR_RULES_OPENER_PHASE_H

namespace Rules {

inline constexpr int kOpenerPhasePieceLimit = 14;

inline bool opener_phase_double_cancel(
    int placedPieces,
    int outgoingAttack,
    int pendingGarbage,
    bool multiplayerEnabled,
    int pieceLimit = kOpenerPhasePieceLimit,
    int sentAttack = 0,
    bool legacy = true) {
    return multiplayerEnabled &&
           pieceLimit > 0 && placedPieces <= pieceLimit &&
           outgoingAttack > 0 &&
           (legacy ? pendingGarbage > outgoingAttack : pendingGarbage >= sentAttack);
}

inline int opener_phase_cancel_budget(int outgoingAttack, bool doubleCancel) {
    return outgoingAttack * (doubleCancel ? 2 : 1);
}

} // namespace Rules

#endif
