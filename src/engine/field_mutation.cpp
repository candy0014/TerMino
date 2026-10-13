#include "tetr/engine/field_mutation.h"

namespace Engine {

std::vector<CellChange> apply_anti_gravity_clear(
    int width,
    int minimumMutableRow,
    const std::vector<int>& clearedRows,
    const CellValue& cellValue,
    const SetCell& setCell) {
    std::vector<CellChange> changes;
    for (auto it = clearedRows.rbegin(); it != clearedRows.rend(); ++it) {
        const int row = *it;
        if (row < minimumMutableRow) continue;
        for (int col = 0; col < width; ++col) {
            (void)cellValue(row, col);
            const auto empty = Runtime::BoardCell::empty();
            setCell(row, col, empty);
            changes.push_back(CellChange{row, col, empty});
        }
    }
    return changes;
}

std::vector<CellChange> apply_gravity_collapse(
    int width,
    const std::vector<Rules::RowMove>& moves,
    const CellValue& cellValue,
    const CopyRow& copyRow) {
    std::vector<CellChange> changes;
    for (const auto& move : moves) {
        for (int col = 0; col < width; ++col) {
            const auto next = cellValue(move.source, col);
            if (cellValue(move.destination, col) != next) {
                changes.push_back(CellChange{move.destination, col, next});
            }
        }
        copyRow(move.destination, move.source);
    }
    return changes;
}

TimedSolidDecayResult decay_timed_solid_garbage(
    int width,
    int firstRow,
    int endRow,
    const CellValue& cellValue,
    const SetCell& setCell) {
    TimedSolidDecayResult out;
    for (int row = firstRow; row < endRow; ++row) {
        bool releaseRow = false;
        for (int col = 0; col < width; ++col) {
            auto value = cellValue(row, col);
            if (value.is_timed_number()) {
                value = value.decremented_timed();
                setCell(row, col, value);
                out.changes.push_back(CellChange{row, col, value});
            }
            if (cellValue(row, col).is_timed_release()) releaseRow = true;
        }
        if (!releaseRow) continue;

        for (int col = 0; col < width; ++col) {
            const auto value = cellValue(row, col).is_timed_release()
                ? Runtime::BoardCell::empty()
                : Runtime::BoardCell::garbage();
            setCell(row, col, value);
            out.changes.push_back(CellChange{row, col, value});
        }
        ++out.releasedRows;
    }
    return out;
}

} // namespace Engine
