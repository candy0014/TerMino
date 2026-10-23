#include "tetr/runtime/playfield.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace Runtime {

Playfield::Row::Row() {
    clear();
}

BoardCell& Playfield::Row::operator[](int col) {
    return cells_[static_cast<std::size_t>(col)];
}

const BoardCell& Playfield::Row::operator[](int col) const {
    return cells_[static_cast<std::size_t>(col)];
}

void Playfield::Row::clear() {
    cells_.fill(BoardCell::empty());
    initialCells_.reset();
    lockTimes_.reset();
}

Playfield::Playfield() = default;

Playfield& Playfield::operator=(const Playfield& other) {
    if (this == &other) return *this;
    if (!other.initialized_) {
        width_ = 0;
        height_ = 0;
        initialized_ = false;
        for (auto& value : positiveRows_) value.clear();
        for (auto& value : negativeRows_) value.clear();
        return *this;
    }
    initialize(other.width_, other.height_);
    for (int rowIndex = 0; rowIndex < height_; ++rowIndex) {
        positiveRows_[static_cast<std::size_t>(rowIndex)] =
            other.positiveRows_[static_cast<std::size_t>(rowIndex)];
    }
    for (int hidden = 0; hidden < Runtime::kHiddenRowCapacity; ++hidden) {
        negativeRows_[static_cast<std::size_t>(hidden)] =
            other.negativeRows_[static_cast<std::size_t>(hidden)];
    }
    initialized_ = other.initialized_;
    return *this;
}

void Playfield::initialize(int width, int height) {
    if (width <= 0 || width > kStorageColumns || height < 0 || height > kStorageRows) {
        throw std::out_of_range("playfield dimensions exceed storage capacity");
    }
    width_ = width;
    height_ = height;
    initialized_ = true;
    clear();
}

void Playfield::clear() {
    for (int rowIndex = 0; rowIndex < height_; ++rowIndex) {
        positiveRows_[static_cast<std::size_t>(rowIndex)].clear();
    }
    for (int hidden = 0; hidden < Runtime::kHiddenRowCapacity; ++hidden) {
        negativeRows_[static_cast<std::size_t>(hidden)].clear();
    }
}

void Playfield::expand(int width, int height, int columnOffset) {
    if (!initialized_ || width < width_ || width > kStorageColumns ||
        height < height_ || height > kStorageRows ||
        columnOffset < 0 || columnOffset > width - width_) {
        throw std::out_of_range("invalid playfield expansion");
    }
    const int rowOffset = height - height_;
    // Bottom-up iteration and a temporary row make both shifts overlap-safe.
    for (int destination = height - 1; destination >= -Runtime::kHiddenRowCapacity; --destination) {
        Row expanded;
        const int source = destination - rowOffset;
        if (source >= -Runtime::kHiddenRowCapacity && source < height_) {
            for (int column = 0; column < width_; ++column) {
                expanded[column + columnOffset] = cell(source, column);
                expanded.initialCells_[column+columnOffset]=row(source).initialCells_[column];
                if(row(source).lockTimes_) {
                    if(!expanded.lockTimes_) expanded.lockTimes_=std::make_shared<std::array<Duration,kStorageColumns>>();
                    (*expanded.lockTimes_)[column+columnOffset]=(*row(source).lockTimes_)[column];
                }
            }
        }
        auto& target = destination >= 0
            ? positiveRows_[static_cast<std::size_t>(destination)]
            : negativeRows_[static_cast<std::size_t>(-destination - 1)];
        target = expanded;
    }
    width_ = width;
    height_ = height;
}

bool Playfield::contains(int rowIndex, int col) const noexcept {
    return col >= 0 && col < width_ &&
           rowIndex >= -Runtime::kHiddenRowCapacity && rowIndex < height_;
}

Playfield::Row& Playfield::row(int rowIndex) {
    if (rowIndex < -Runtime::kHiddenRowCapacity || rowIndex >= height_) {
        throw std::out_of_range("playfield row outside storage");
    }
    if (rowIndex >= 0) return positiveRows_[static_cast<std::size_t>(rowIndex)];
    return negativeRows_[static_cast<std::size_t>(-rowIndex - 1)];
}

const Playfield::Row& Playfield::row(int rowIndex) const {
    if (rowIndex < -Runtime::kHiddenRowCapacity || rowIndex >= height_) {
        throw std::out_of_range("playfield row outside storage");
    }
    if (rowIndex >= 0) return positiveRows_[static_cast<std::size_t>(rowIndex)];
    return negativeRows_[static_cast<std::size_t>(-rowIndex - 1)];
}

BoardCell& Playfield::cell(int rowIndex, int col) {
    if (col < 0 || col >= width_) throw std::out_of_range("playfield column outside storage");
    return row(rowIndex)[col];
}

const BoardCell& Playfield::cell(int rowIndex, int col) const {
    if (col < 0 || col >= width_) throw std::out_of_range("playfield column outside storage");
    return row(rowIndex)[col];
}

void Playfield::set_cell(int rowIndex, int col, BoardCell value) {
    cell(rowIndex, col) = value;
    row(rowIndex).initialCells_.reset(col);
    if(row(rowIndex).lockTimes_) mark_cell_lock_time(rowIndex,col,{});
}

void Playfield::copy_row(int destination, int source) {
    row(destination) = row(source);
}

Duration Playfield::cell_lock_time(int rowIndex,int column) const {
    if(!contains(rowIndex,column)) return {};
    const auto& times=row(rowIndex).lockTimes_;
    return times?(*times)[column]:Duration{};
}

void Playfield::mark_cell_lock_time(int rowIndex,int column,Duration elapsed) {
    if(!contains(rowIndex,column)) throw std::out_of_range("playfield cell outside storage");
    auto& times=row(rowIndex).lockTimes_;
    if(!times) {
        if(elapsed==Duration{}) return;
        times=std::make_shared<std::array<Duration,kStorageColumns>>();
    } else if(!times.unique()) times=std::make_shared<std::array<Duration,kStorageColumns>>(*times);
    (*times)[column]=elapsed;
}
void Playfield::mark_initial_cell(int rowIndex,int column) {
    row(rowIndex).initialCells_.set(column,cell(rowIndex,column).occupied());
}
bool Playfield::initial_cell(int rowIndex,int column) const {
    return cell(rowIndex,column).occupied() && row(rowIndex).initialCells_[column];
}
int Playfield::remaining_initial_cells() const {
    if(!initialized_) return 0;
    int count=0;
    for(int r=-Runtime::kHiddenRowCapacity;r<height_;++r) {
        const auto& value=row(r);
        if(!value.initialCells_.any()) continue;
        for(int c=0;c<width_;++c) if(value.initialCells_[c] && value[c].occupied()) ++count;
    }
    return count;
}

} // namespace Runtime
