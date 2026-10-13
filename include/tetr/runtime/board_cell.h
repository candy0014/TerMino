#ifndef TETR_RUNTIME_BOARD_CELL_H
#define TETR_RUNTIME_BOARD_CELL_H

namespace Runtime {

enum class BoardCellKind {
    Empty,
    Piece,
    Garbage,
    SolidGarbage,
    TimedSolid,
    TimedRelease
};

class BoardCell {
public:
    constexpr BoardCell() noexcept = default;

    static constexpr BoardCell empty() noexcept { return BoardCell(BoardCellKind::Empty, -1, 0); }
    static constexpr BoardCell piece(int colorId) noexcept { return BoardCell(BoardCellKind::Piece, colorId, 0); }
    static constexpr BoardCell garbage() noexcept { return BoardCell(BoardCellKind::Garbage, 7, 0); }
    static constexpr BoardCell solid_garbage() noexcept { return BoardCell(BoardCellKind::SolidGarbage, 8, 0); }
    // countdown == 0 is the historical plain timed-solid cell (legacy value 9).
    static constexpr BoardCell timed_solid(int countdown = 0) noexcept {
        return BoardCell(BoardCellKind::TimedSolid, 9, countdown < 0 ? 0 : countdown);
    }
    static constexpr BoardCell timed_release() noexcept { return BoardCell(BoardCellKind::TimedRelease, 9, 0); }

    constexpr BoardCellKind kind() const noexcept { return kind_; }
    constexpr bool empty_cell() const noexcept { return kind_ == BoardCellKind::Empty; }
    constexpr bool occupied() const noexcept { return !empty_cell(); }
    constexpr bool is_piece() const noexcept { return kind_ == BoardCellKind::Piece; }
    constexpr bool is_normal_garbage() const noexcept { return kind_ == BoardCellKind::Garbage; }
    constexpr bool is_garbage_like() const noexcept {
        return kind_ == BoardCellKind::Garbage || kind_ == BoardCellKind::SolidGarbage ||
               kind_ == BoardCellKind::TimedSolid || kind_ == BoardCellKind::TimedRelease;
    }
    constexpr bool is_timed_number() const noexcept {
        return kind_ == BoardCellKind::TimedSolid && countdown_ > 0;
    }
    constexpr bool is_timed_release() const noexcept { return kind_ == BoardCellKind::TimedRelease; }
    constexpr int timed_countdown() const noexcept { return is_timed_number() ? countdown_ : 0; }
    constexpr int color_id() const noexcept { return colorId_; }

    constexpr BoardCell decremented_timed() const noexcept {
        if (!is_timed_number()) return *this;
        return countdown_ == 1 ? timed_release() : timed_solid(countdown_ - 1);
    }

    friend constexpr bool operator==(const BoardCell& lhs, const BoardCell& rhs) noexcept {
        return lhs.kind_ == rhs.kind_ && lhs.colorId_ == rhs.colorId_ && lhs.countdown_ == rhs.countdown_;
    }
    friend constexpr bool operator!=(const BoardCell& lhs, const BoardCell& rhs) noexcept { return !(lhs == rhs); }

private:
    constexpr BoardCell(BoardCellKind kind, int colorId, int countdown) noexcept
        : kind_(kind), colorId_(colorId), countdown_(countdown) {}

    BoardCellKind kind_ = BoardCellKind::Empty;
    int colorId_ = -1;
    int countdown_ = 0;
};

} // namespace Runtime

#endif
