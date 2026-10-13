#ifndef TETR_RUNTIME_STRONG_TYPES_H
#define TETR_RUNTIME_STRONG_TYPES_H

#include <type_traits>

namespace Runtime {

class Rotation {
public:
    constexpr Rotation() noexcept = default;
    explicit constexpr Rotation(int value) noexcept : value_(normalize(value)) {}

    static constexpr Rotation from_index(int value) noexcept { return Rotation(value); }
    constexpr int index() const noexcept { return value_; }
    constexpr Rotation rotated(int delta) const noexcept { return Rotation(value_ + delta); }

    friend constexpr bool operator==(Rotation lhs, Rotation rhs) noexcept { return lhs.value_ == rhs.value_; }
    friend constexpr bool operator!=(Rotation lhs, Rotation rhs) noexcept { return !(lhs == rhs); }

private:
    static constexpr int normalize(int value) noexcept {
        const int mod = value % 4;
        return mod < 0 ? mod + 4 : mod;
    }
    int value_ = 0;
};

struct CellPosition {
    int row = -2;
    int col = 0;

    constexpr CellPosition translated(int rowDelta, int colDelta) const noexcept {
        return CellPosition{row + rowDelta, col + colDelta};
    }
    friend constexpr bool operator==(const CellPosition& lhs, const CellPosition& rhs) noexcept {
        return lhs.row == rhs.row && lhs.col == rhs.col;
    }
    friend constexpr bool operator!=(const CellPosition& lhs, const CellPosition& rhs) noexcept { return !(lhs == rhs); }
};

} // namespace Runtime

namespace Piece {

class Type {
public:
    constexpr Type() noexcept = default;
    explicit constexpr Type(int value) noexcept : value_(value) {}

    static constexpr Type from_index(int value) noexcept { return Type(value); }
    constexpr int index() const noexcept { return value_; }
    static constexpr Type none() noexcept { return Type(-1); }
    constexpr bool valid() const noexcept { return value_ >= 0; }

    friend constexpr bool operator==(Type lhs, Type rhs) noexcept { return lhs.value_ == rhs.value_; }
    friend constexpr bool operator!=(Type lhs, Type rhs) noexcept { return !(lhs == rhs); }
    friend constexpr bool operator<(Type lhs, Type rhs) noexcept { return lhs.value_ < rhs.value_; }

private:
    int value_ = -1;
};

class KickIndex {
public:
    constexpr KickIndex() noexcept = default;
    explicit constexpr KickIndex(int value) noexcept : value_(value) {}

    static constexpr KickIndex from_index(int value) noexcept { return KickIndex(value); }
    constexpr int index() const noexcept { return value_; }
    friend constexpr bool operator==(KickIndex lhs, KickIndex rhs) noexcept { return lhs.value_ == rhs.value_; }
    friend constexpr bool operator!=(KickIndex lhs, KickIndex rhs) noexcept { return !(lhs == rhs); }

private:
    int value_ = -1;
};

struct Pose {
    Runtime::CellPosition position{};
    Runtime::Rotation rotation{};

    constexpr Pose translated(int rowDelta, int colDelta) const noexcept {
        return Pose{position.translated(rowDelta, colDelta), rotation};
    }
    friend constexpr bool operator==(const Pose& lhs, const Pose& rhs) noexcept {
        return lhs.position == rhs.position && lhs.rotation == rhs.rotation;
    }
    friend constexpr bool operator!=(const Pose& lhs, const Pose& rhs) noexcept { return !(lhs == rhs); }
};

} // namespace Piece

namespace Runtime {


class MultiplayerSequenceId {
public:
    constexpr MultiplayerSequenceId() noexcept = default;
    explicit constexpr MultiplayerSequenceId(int value) noexcept : value_(value) {}

    static constexpr MultiplayerSequenceId from_wire(int value) noexcept { return MultiplayerSequenceId(value); }
    constexpr int value() const noexcept { return value_; }

    constexpr MultiplayerSequenceId& operator++() noexcept {
        ++value_;
        return *this;
    }
    constexpr MultiplayerSequenceId operator++(int) noexcept {
        const MultiplayerSequenceId previous = *this;
        ++*this;
        return previous;
    }

    friend constexpr bool operator==(MultiplayerSequenceId lhs, MultiplayerSequenceId rhs) noexcept { return lhs.value_ == rhs.value_; }
    friend constexpr bool operator!=(MultiplayerSequenceId lhs, MultiplayerSequenceId rhs) noexcept { return !(lhs == rhs); }
    friend constexpr bool operator<(MultiplayerSequenceId lhs, MultiplayerSequenceId rhs) noexcept { return lhs.value_ < rhs.value_; }
    friend constexpr bool operator<=(MultiplayerSequenceId lhs, MultiplayerSequenceId rhs) noexcept { return lhs.value_ <= rhs.value_; }

private:
    int value_ = 0;
};

class RotationRecord {
public:
    constexpr RotationRecord() noexcept = default;

    static constexpr RotationRecord none() noexcept { return RotationRecord(); }
    static constexpr RotationRecord from_kick(Piece::KickIndex kick) noexcept {
        RotationRecord result;
        result.rotated_ = true;
        result.kick_ = kick;
        return result;
    }
    constexpr bool rotated() const noexcept { return rotated_; }
    constexpr Piece::KickIndex kick() const noexcept { return kick_; }

    friend constexpr bool operator==(const RotationRecord& lhs, const RotationRecord& rhs) noexcept {
        return lhs.rotated_ == rhs.rotated_ && (!lhs.rotated_ || lhs.kick_ == rhs.kick_);
    }
    friend constexpr bool operator!=(const RotationRecord& lhs, const RotationRecord& rhs) noexcept { return !(lhs == rhs); }

private:
    bool rotated_ = false;
    Piece::KickIndex kick_{};
};

} // namespace Runtime

#endif
