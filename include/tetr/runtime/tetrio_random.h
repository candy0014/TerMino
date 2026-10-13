#ifndef TETR_RUNTIME_TETRIO_RANDOM_H
#define TETR_RUNTIME_TETRIO_RANDOM_H
#include <array>
#include <cstdint>
#include <utility>

namespace Runtime {
// Opt-in replay interoperability profile. Normal games keep their mt19937.
class TetrioRandom {
public:
    explicit TetrioRandom(std::int64_t seed = 1) { reseed(seed); }
    void reseed(std::int64_t seed) {
        seed_ = seed % 2147483647;
        if (seed_ <= 0) seed_ += 2147483646;
        if (!seed_) seed_ = 1;
    }
    std::int64_t next() { return seed_ = seed_ * 16807 % 2147483647; }
    double uniform() { return double(next() - 1) / 2147483646.0; }
    std::int64_t state() const { return seed_; }
    std::array<int, 7> bag() {
        std::array<int, 7> result{6, 2, 3, 4, 0, 1, 5}; // Z L O S I J T
        for (int i = 6; i > 0; --i) std::swap(result[i], result[int(uniform() * (i + 1))]);
        return result;
    }
private:
    std::int64_t seed_ = 1;
};

struct TetrioGarbageRandom {
    TetrioRandom random;
    int column = -1;
    explicit TetrioGarbageRandom(std::int64_t seed = 1) : random(seed) {}
    int choose_column(int width, bool avoidSame = false) {
        if (width <= 1) { (void)random.uniform(); return column = 0; }
        if (avoidSame && column >= 0 && column < width) {
            int nextColumn = int(random.uniform() * (width - 1));
            if (nextColumn >= column) ++nextColumn;
            return column = nextColumn;
        }
        return column = int(random.uniform() * width);
    }
    int row(int width, double messiness = 0, bool avoidSame = false) {
        if (column < 0 || column >= width) choose_column(width,avoidSame);
        else if (random.uniform() < messiness) choose_column(width,avoidSame);
        return column;
    }
    void finish_packet(int width, bool avoidSame = false) {
        (void)random.uniform(); // messiness_change=1
        choose_column(width,avoidSame);
    }
};
} // namespace Runtime
#endif
