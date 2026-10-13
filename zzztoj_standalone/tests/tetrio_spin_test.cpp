#include "tetr_spin.h"

#include <array>
#include <iostream>

int main() {
    int cases = 0;
    for (const char piece : {'I', 'J', 'L', 'O', 'S', 'T', 'Z'}) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            for (int rotated = 0; rotated <= 1; ++rotated) {
                for (int kick = 0; kick <= 5; ++kick) {
                    for (int immobile = 0; immobile <= 1; ++immobile) {
                        for (int mask = 0; mask < 16; ++mask) {
                            std::array<bool, 4> corners{};
                            int occupied = 0;
                            int front = 0;
                            for (int corner = 0; corner < 4; ++corner) {
                                corners[corner] = (mask & (1 << corner)) != 0;
                                if (!corners[corner]) continue;
                                ++occupied;
                                if (corner == rotation || corner == (rotation + 1) % 4) {
                                    ++front;
                                }
                            }

                            bool expectedSpin = false;
                            bool expectedMini = false;
                            if (rotated != 0) {
                                if (piece != 'T') {
                                    expectedSpin = immobile != 0;
                                    expectedMini = expectedSpin;
                                } else if (occupied >= 3 &&
                                           (front == 2 || kick == 4)) {
                                    expectedSpin = true;
                                } else if (immobile != 0 || occupied == 3) {
                                    expectedSpin = true;
                                    expectedMini = true;
                                }
                            }

                            const auto actual = TetrSpin::classify({
                                piece, rotation, rotated != 0, kick,
                                immobile != 0, corners
                            });
                            if (actual.spin != expectedSpin ||
                                actual.mini != expectedMini) {
                                std::cerr << "All-Mini+ mismatch: piece=" << piece
                                          << " rotation=" << rotation
                                          << " rotated=" << rotated
                                          << " kick=" << kick
                                          << " immobile=" << immobile
                                          << " mask=" << mask << '\n';
                                return 1;
                            }
                            ++cases;
                        }
                    }
                }
            }
        }
    }
    std::cout << "matched " << cases
              << " current TETR.IO All-Mini+ classifications\n";
    return 0;
}
