#ifndef ZZZTOJ_STANDALONE_TETR_SPIN_H
#define ZZZTOJ_STANDALONE_TETR_SPIN_H

#include <array>

namespace TetrSpin {

struct Input {
    char piece = ' ';
    int rotation = 0;
    bool lastRotated = false;
    int lastKickIndex = 0;
    bool immobile = false;
    // T-corner order from tetr src/rules/spin.cpp:
    // upper-left, upper-right, lower-right, lower-left.
    std::array<bool, 4> occupiedCorners{};
};

struct Result {
    bool spin = false;
    bool mini = false;
};

// Current TETR.IO All-Mini+ classification: all immobile non-T rotations are
// Mini Spins; T uses its normal three-corner full/mini test and additionally
// accepts otherwise-immobile rotated placements as Mini Spins.
Result classify(const Input& input);

} // namespace TetrSpin

#endif
