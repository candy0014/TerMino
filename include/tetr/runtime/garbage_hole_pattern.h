#ifndef TETR_RUNTIME_GARBAGE_HOLE_PATTERN_H
#define TETR_RUNTIME_GARBAGE_HOLE_PATTERN_H

namespace Runtime {

enum class GarbageHoleLayout {
    Scattered,
    Contiguous
};

// Semantic form of the historical HoleNum value. Non-negative legacy values
// mean a number of scattered holes; negative values mean one contiguous run.
class GarbageHolePattern {
public:
    constexpr GarbageHolePattern() = default;

    static constexpr GarbageHolePattern scattered(int count) noexcept {
        return GarbageHolePattern(GarbageHoleLayout::Scattered, count);
    }
    static constexpr GarbageHolePattern contiguous(int length) noexcept {
        return GarbageHolePattern(GarbageHoleLayout::Contiguous, length);
    }
    constexpr GarbageHoleLayout layout() const noexcept { return layout_; }
    constexpr int extent() const noexcept { return extent_; }
    constexpr bool is_contiguous() const noexcept {
        return layout_ == GarbageHoleLayout::Contiguous;
    }
    friend constexpr bool operator==(GarbageHolePattern lhs, GarbageHolePattern rhs) noexcept {
        return lhs.layout_ == rhs.layout_ && lhs.extent_ == rhs.extent_;
    }
    friend constexpr bool operator!=(GarbageHolePattern lhs, GarbageHolePattern rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    constexpr GarbageHolePattern(GarbageHoleLayout layout, int extent) noexcept
        : layout_(layout), extent_(extent) {}

    GarbageHoleLayout layout_ = GarbageHoleLayout::Scattered;
    int extent_ = 1;
};

} // namespace Runtime

#endif
