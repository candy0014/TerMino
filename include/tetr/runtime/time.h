#ifndef TETR_RUNTIME_TIME_H
#define TETR_RUNTIME_TIME_H

namespace Runtime {

// Monotonic duration measured in seconds. The representation remains double
// to preserve historical thresholds and serialized values; the type prevents
// elapsed durations from being mixed with absolute timestamps.
class Duration {
public:
    constexpr Duration() = default;

    static constexpr Duration from_seconds(double value) { return Duration(value); }

    constexpr double seconds() const { return seconds_; }

    constexpr Duration& operator+=(Duration other) {
        seconds_ += other.seconds_;
        return *this;
    }
    constexpr Duration& operator-=(Duration other) {
        seconds_ -= other.seconds_;
        return *this;
    }

private:
    explicit constexpr Duration(double value) : seconds_(value) {}
    double seconds_ = 0.0;
};

constexpr Duration operator+(Duration lhs, Duration rhs) {
    lhs += rhs;
    return lhs;
}
constexpr Duration operator-(Duration lhs, Duration rhs) {
    lhs -= rhs;
    return lhs;
}
constexpr Duration operator*(Duration value, double scale) {
    return Duration::from_seconds(value.seconds() * scale);
}
constexpr Duration operator*(double scale, Duration value) { return value * scale; }
constexpr Duration operator/(Duration value, double scale) {
    return Duration::from_seconds(value.seconds() / scale);
}
constexpr double operator/(Duration lhs, Duration rhs) {
    return lhs.seconds() / rhs.seconds();
}
constexpr bool operator==(Duration lhs, Duration rhs) { return lhs.seconds() == rhs.seconds(); }
constexpr bool operator!=(Duration lhs, Duration rhs) { return !(lhs == rhs); }
constexpr bool operator<(Duration lhs, Duration rhs) { return lhs.seconds() < rhs.seconds(); }
constexpr bool operator<=(Duration lhs, Duration rhs) { return lhs.seconds() <= rhs.seconds(); }
constexpr bool operator>(Duration lhs, Duration rhs) { return rhs < lhs; }
constexpr bool operator>=(Duration lhs, Duration rhs) { return rhs <= lhs; }

// Absolute point on the process monotonic clock.  It has no arithmetic with
// raw doubles: subtracting two points yields Duration, and only Duration may be
// added to/subtracted from a point.
class TimePoint {
public:
    constexpr TimePoint() = default;

    static constexpr TimePoint from_seconds(double value) { return TimePoint(value); }

    constexpr double seconds() const { return seconds_; }

    constexpr TimePoint& operator+=(Duration delta) {
        seconds_ += delta.seconds();
        return *this;
    }
    constexpr TimePoint& operator-=(Duration delta) {
        seconds_ -= delta.seconds();
        return *this;
    }

private:
    explicit constexpr TimePoint(double value) : seconds_(value) {}
    double seconds_ = 0.0;
};

constexpr Duration operator-(TimePoint lhs, TimePoint rhs) {
    return Duration::from_seconds(lhs.seconds() - rhs.seconds());
}
constexpr TimePoint operator+(TimePoint point, Duration delta) {
    point += delta;
    return point;
}
constexpr TimePoint operator+(Duration delta, TimePoint point) { return point + delta; }
constexpr TimePoint operator-(TimePoint point, Duration delta) {
    point -= delta;
    return point;
}
constexpr bool operator==(TimePoint lhs, TimePoint rhs) { return lhs.seconds() == rhs.seconds(); }
constexpr bool operator!=(TimePoint lhs, TimePoint rhs) { return !(lhs == rhs); }
constexpr bool operator<(TimePoint lhs, TimePoint rhs) { return lhs.seconds() < rhs.seconds(); }
constexpr bool operator<=(TimePoint lhs, TimePoint rhs) { return lhs.seconds() <= rhs.seconds(); }
constexpr bool operator>(TimePoint lhs, TimePoint rhs) { return rhs < lhs; }
constexpr bool operator>=(TimePoint lhs, TimePoint rhs) { return rhs <= lhs; }

} // namespace Runtime

#endif
