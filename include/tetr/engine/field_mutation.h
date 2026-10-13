#ifndef TETR_ENGINE_FIELD_MUTATION_H
#define TETR_ENGINE_FIELD_MUTATION_H

#include "tetr/rules/line_clear.h"
#include "tetr/runtime/board_cell.h"

#include <functional>
#include <vector>

namespace Engine {

struct CellChange {
    int row = 0;
    int col = 0;
    Runtime::BoardCell value = Runtime::BoardCell::empty();
};

using CellValue = std::function<Runtime::BoardCell(int row, int col)>;
using SetCell = std::function<void(int row, int col, Runtime::BoardCell value)>;
using CopyRow = std::function<void(int destination, int source)>;

std::vector<CellChange> apply_anti_gravity_clear(
    int width,
    int minimumMutableRow,
    const std::vector<int>& clearedRows,
    const CellValue& cellValue,
    const SetCell& setCell);

std::vector<CellChange> apply_gravity_collapse(
    int width,
    const std::vector<Rules::RowMove>& moves,
    const CellValue& cellValue,
    const CopyRow& copyRow);

struct TimedSolidDecayResult {
    std::vector<CellChange> changes;
    int releasedRows = 0;
};

TimedSolidDecayResult decay_timed_solid_garbage(
    int width,
    int firstRow,
    int endRow,
    const CellValue& cellValue,
    const SetCell& setCell);

} // namespace Engine

#endif
