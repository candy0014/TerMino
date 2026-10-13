#include "tetr/engine/garbage_field.h"

namespace Engine {

GarbageShiftResult shift_field_up_for_garbage(
    int width,
    int firstRow,
    int bottomExclusive,
    int riseLines,
    const CellValue& cellValue,
    const CopyRow& copyRow,
    bool collectChanges) {
    GarbageShiftResult result;
    for (int row = firstRow; row < bottomExclusive - riseLines; ++row) {
        const int source = row + riseLines;
        if (collectChanges) for (int col = 0; col < width; ++col) {
            const auto next = cellValue(source, col);
            if (cellValue(row, col) != next) {
                result.changes.push_back(CellChange{row, col, next});
            }
        }
        copyRow(row, source);
    }
    return result;
}

std::vector<CellChange> write_generated_row(
    int row,
    const std::vector<Runtime::BoardCell>& values,
    const CellValue& cellValue,
    const SetCell& setCell,
    bool collectChanges) {
    std::vector<CellChange> changes;
    for (int col = 0; col < static_cast<int>(values.size()); ++col) {
        if (collectChanges && cellValue(row, col) != values[col]) {
            changes.push_back(CellChange{row, col, values[col]});
        }
        setCell(row, col, values[col]);
    }
    return changes;
}

} // namespace Engine
