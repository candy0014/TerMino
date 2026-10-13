#ifndef TETR_APP_FIRST_TO_SEVEN_H
#define TETR_APP_FIRST_TO_SEVEN_H
#include <array>
#include <stdexcept>
namespace App {
class FirstToSeven {
public:
    bool finished() const { return wins_[0] == 7 || wins_[1] == 7; }
    int wins(int side) const { return wins_.at(side); }
    int rounds() const { return wins_[0] + wins_[1]; }
    // Only an actual, single-winner host result can advance the tournament.
    void accept_score(int first, int second) {
        if (finished() || first < wins_[0] || second < wins_[1] ||
            first > 7 || second > 7 || (first - wins_[0]) + (second - wins_[1]) != 1)
            throw std::runtime_error("Round did not finish with exactly one winner, or match already ended");
        wins_ = {first, second};
    }
private:
    std::array<int, 2> wins_{};
};
}
#endif
