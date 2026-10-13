#include "tetr_attack.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

int expected_basic(const TetrAttack::Input& input) {
    if (input.linesCleared <= 0) return 0;
    if (input.spin && !input.mini) {
        return input.linesCleared >= 4
            ? 2 * (input.linesCleared + 1)
            : 2 * input.linesCleared;
    }
    static constexpr int kNormalMini[4] = {0, 1, 2, 4};
    if (input.linesCleared <= 4) return kNormalMini[input.linesCleared - 1];
    return input.linesCleared;
}

} // namespace

int main() {
    int cases = 0;
    for (int currentB2B = 0; currentB2B <= 8; ++currentB2B) {
        for (int currentCombo = 0; currentCombo <= 50; ++currentCombo) {
            for (int lines = 0; lines <= 6; ++lines) {
                for (int spin = 0; spin <= 1; ++spin) {
                    for (int mini = 0; mini <= 1; ++mini) {
                        for (int perfectClear = 0; perfectClear <= 1; ++perfectClear) {
                            const TetrAttack::Input input{
                                lines, spin != 0, mini != 0, perfectClear != 0
                            };
                            const auto actual = TetrAttack::calculate(
                                {currentB2B, currentCombo}, input);

                            const bool difficult = lines > 0 &&
                                (lines >= 4 || spin != 0 || perfectClear != 0);
                            const bool breaksB2B = lines > 0 && !difficult;
                            const int expectedB2B = difficult
                                ? currentB2B + 1
                                : (breaksB2B ? 0 : currentB2B);
                            const int expectedCombo = lines > 0 ? currentCombo + 1 : 0;
                            const int expectedSurge = breaksB2B && currentB2B >= 5
                                ? currentB2B - 1
                                : 0;

                            int expectedAttack = 0;
                            if (lines > 0) {
                                const int combo = expectedCombo - 1;
                                const int b2bBonus = expectedB2B >= 2 ? 1 : 0;
                                double raw = (expected_basic(input) + b2bBonus) *
                                    (1.0 + 0.25 * combo);
                                if (combo > 1) {
                                    raw = std::max(raw, std::log(1.25 * combo + 1.0));
                                }
                                expectedAttack = static_cast<int>(std::floor(raw)) +
                                    (perfectClear ? 5 : 0) + expectedSurge;
                            }

                            if (actual.attack != expectedAttack ||
                                actual.state.b2b != expectedB2B ||
                                actual.state.combo != expectedCombo ||
                                actual.b2bCharging != expectedSurge) {
                                std::cerr << "TETR.IO spec mismatch: b2b=" << currentB2B
                                          << " combo=" << currentCombo
                                          << " lines=" << lines
                                          << " spin=" << spin
                                          << " mini=" << mini
                                          << " pc=" << perfectClear
                                          << " actual=" << actual.attack
                                          << " expected=" << expectedAttack << '\n';
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
              << " current TETR.IO attack/chain cases\n";
    return 0;
}
