#ifndef TETR_RUNTIME_MOVEMENT_RESTRICTIONS_H
#define TETR_RUNTIME_MOVEMENT_RESTRICTIONS_H

namespace Runtime {
struct MovementRestrictions {
    bool disableLeft = false;
    bool disableRight = false;
    bool disableClockwise = false;
    bool disableCounterclockwise = false;
    bool disable180 = false;
    bool disableSoftDrop = false;
    bool disableHardDrop = false;

    bool allows_horizontal(int direction) const noexcept {
        return direction < 0 ? !disableLeft : direction > 0 ? !disableRight : true;
    }
    bool allows_rotation(int delta) const noexcept {
        const int turns = (delta % 4 + 4) % 4;
        return turns == 1 ? !disableClockwise : turns == 2 ? !disable180
            : turns == 3 ? !disableCounterclockwise : true;
    }
    unsigned mask() const noexcept {
        return unsigned(disableLeft) | unsigned(disableRight) << 1 | unsigned(disableClockwise) << 2 |
            unsigned(disableCounterclockwise) << 3 | unsigned(disable180) << 4 |
            unsigned(disableSoftDrop) << 5 | unsigned(disableHardDrop) << 6;
    }
    static MovementRestrictions from_mask(unsigned mask) noexcept {
        return {bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8),
            bool(mask & 16), bool(mask & 32), bool(mask & 64)};
    }
};
} // namespace Runtime
#endif
