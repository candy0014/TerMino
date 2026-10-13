#ifndef TETR_RULES_LOCK_ANALYSIS_H
#define TETR_RULES_LOCK_ANALYSIS_H

#include "tetr/runtime/board_cell.h"

#include <functional>
#include <vector>

namespace Rules {

struct ClearAnalysis {
    std::vector<int> clearedRows;
    int clearedLines = 0;
    int clearedGarbageLines = 0;
    int clearedLayerLines = 0;
    bool perfectClear = true;
    bool halfClear = false;
};

using CellValue = std::function<Runtime::BoardCell(int row, int col)>;

ClearAnalysis analyze_locked_field(
    int width,
    int firstRow,
    int endRow,
    int garbageStartRow,
    int layerStartRow,
    const CellValue& cellValue);

} // namespace Rules

#endif
