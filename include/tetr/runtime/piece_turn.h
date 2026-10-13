#ifndef TETR_RUNTIME_PIECE_TURN_H
#define TETR_RUNTIME_PIECE_TURN_H

#include <cmath>
#include <cstdint>
#include <limits>

namespace Runtime {
inline std::uint64_t completed_piece_turns(std::uint64_t pieces, double quota) noexcept {
    if (!(quota > 0.0)) return 0;
    long double covered = static_cast<long double>(pieces) / quota;
    const long double nearest = std::round(covered);
    if (std::fabs(covered - nearest) <=
        4 * std::numeric_limits<double>::epsilon() * std::fabs(covered))
        covered = nearest;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return covered >= static_cast<long double>(maximum)
        ? maximum : static_cast<std::uint64_t>(covered);
}
} // namespace Runtime
#endif
