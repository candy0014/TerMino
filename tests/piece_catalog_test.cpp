#include "tetr/runtime/piece.h"

#include <cassert>
#include <vector>

int main() {
    const auto& catalog = Piece::catalog();
    assert(catalog.size() == 229);
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        const auto& entry = catalog[index];
        assert(entry.type.index() == static_cast<int>(index));
        assert(Piece::metadata(entry.type).name == entry.metadata.name);
        assert(Piece::get_definition(entry.type).color_id() == entry.metadata.colorId);
        assert(entry.metadata.nameWidth >= 0);
        assert(entry.metadata.displayHeight >= 0);
        assert(entry.metadata.displayWidth >= 0);
    }

    const int expectedSizes[] = {7, 7, 7, 18, 4, 60, 77};
    for (int system = 0; system < 7; ++system) {
        const std::vector<Piece::Type> types = Piece::types_for_rotation_system(
            Runtime::rotation_system_from_index(system));
        assert(static_cast<int>(types.size()) == expectedSizes[system]);
        for (Piece::Type type : types) {
            if (system == 6 && type.index() < 7) continue;
            assert(static_cast<int>(Piece::metadata(type).rotationSystem) == system);
        }
    }
}
