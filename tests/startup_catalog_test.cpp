#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/piece.h"

#include <cassert>
#include <cstddef>

int main() {
    const auto srsPlus = Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    assert(srsPlus.size() == static_cast<std::size_t>(Piece::standard_tetromino_count()));
    const auto& first = Piece::catalog();
    const auto& second = Piece::catalog();
    assert(!first.empty());
    assert(&first == &second);
    assert(first.size() >= srsPlus.size());
    return 0;
}
