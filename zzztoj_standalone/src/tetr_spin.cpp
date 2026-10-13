#include "tetr_spin.h"

namespace TetrSpin {

Result classify(const Input& input) {
    Result result;
    if (!input.lastRotated) return result;

    // All-Mini+: non-T immobility spins always use the Mini attack row.
    if (input.piece != 'T') {
        result.spin = input.immobile;
        result.mini = input.immobile;
        return result;
    }

    int occupiedCorners = 0;
    int frontCorners = 0;
    for (int corner = 0; corner < 4; ++corner) {
        if (!input.occupiedCorners[corner]) continue;
        ++occupiedCorners;
        if (corner == input.rotation || corner == (input.rotation + 1) % 4) {
            ++frontCorners;
        }
    }

    if (occupiedCorners >= 3 &&
        (frontCorners == 2 || input.lastKickIndex == 4)) {
        result.spin = true;
    } else if (input.immobile || occupiedCorners == 3) {
        result.spin = true;
        result.mini = true;
    }
    return result;
}

} // namespace TetrSpin
