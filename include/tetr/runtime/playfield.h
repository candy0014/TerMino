#ifndef TETR_RUNTIME_PLAYFIELD_H
#define TETR_RUNTIME_PLAYFIELD_H

#include "tetr/runtime/board_cell.h"
#include "tetr/runtime/constants.h"

#include <array>
#include <bitset>

namespace Runtime {

class Playfield {
public:
    static constexpr int kStorageRows = 105;
    static constexpr int kStorageColumns = 105;

    class Row {
    public:
        Row();

        BoardCell& operator[](int col);
        const BoardCell& operator[](int col) const;
        void clear();

    private:
        friend class Playfield;
        std::array<BoardCell, kStorageColumns> cells_{};
        std::bitset<kStorageColumns> initialCells_;
    };

    Playfield();
    Playfield(const Playfield&) = default;
    Playfield& operator=(const Playfield& other);

    void initialize(int width, int height);
    // Add empty space above and at the sides, preserving bottom-relative cells.
    void expand(int width, int height, int columnOffset);
    void clear();

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    bool initialized() const noexcept { return initialized_; }

    bool contains(int row, int col) const noexcept;

    Row& row(int rowIndex);
    const Row& row(int rowIndex) const;
    BoardCell& cell(int rowIndex, int col);
    const BoardCell& cell(int rowIndex, int col) const;
    void set_cell(int rowIndex, int col, BoardCell value);
    void copy_row(int destination, int source);
    void mark_initial_cell(int row,int column);
    bool initial_cell(int row,int column) const;
    int remaining_initial_cells() const;

private:
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
    std::array<Row, kStorageRows> positiveRows_{};
    std::array<Row, kStorageRows> negativeRows_{};
};

} // namespace Runtime

#endif
