#include "tetr/util/bit_ops.h"

#include <cassert>
#include <cstdint>

int main() {
    for (std::uint32_t value = 0; value <= 0xffffu; ++value) {
        int bits = 0;
        int zeros = 32;
        for (int index = 0; index < 32; ++index) {
            if (value & (std::uint32_t{1} << index)) {
                ++bits;
                if (zeros == 32) zeros = index;
            }
        }
        assert(Util::popcount32(value) == bits);
        assert(Util::trailing_zeros32(value) == zeros);
        assert(Util::popcount64((std::uint64_t{value} << 32) | value) == 2 * bits);
    }
    for (int index = 0; index < 32; ++index)
        assert(Util::trailing_zeros32(std::uint32_t{1} << index) == index);
    for (int index = 0; index < 64; ++index)
        assert(Util::popcount64(std::uint64_t{1} << index) == 1);
    assert(Util::popcount32(0xffffffffu) == 32);
    assert(Util::popcount64(0xffffffffffffffffull) == 64);
    assert(Util::popcount64(0) == 0);
}
