#include "tetr/rules/lock_analysis.h"

namespace Rules {

ClearAnalysis analyze_locked_field(
    int width,
    int firstRow,
    int endRow,
    int garbageStartRow,
    int layerStartRow,
    const CellValue& cellValue) {
    ClearAnalysis result;
    bool remainingPiece = false;

    for (int row = firstRow; row < endRow; ++row) {
        bool full = true;
        for (int col = 0; col < width; ++col) {
            if (cellValue(row, col).empty_cell()) {
                full = false;
                break;
            }
        }

        if (full) {
            result.clearedRows.push_back(row);
            ++result.clearedLines;
            if (row >= garbageStartRow) ++result.clearedGarbageLines;
            if (row >= layerStartRow) ++result.clearedLayerLines;
        } else {
            for (int col = 0; col < width; ++col)
                remainingPiece = remainingPiece || cellValue(row, col).is_piece();
        }

        // The legacy perfect-clear check compared only empty vs occupied state.
        if (result.perfectClear && width > 0) {
            const bool firstOccupied = cellValue(row, 0).occupied();
            for (int col = 1; col < width; ++col) {
                if (cellValue(row, col).occupied() != firstOccupied) {
                    result.perfectClear = false;
                    break;
                }
            }
        }
    }
    result.halfClear = result.clearedLines > 0 && !result.perfectClear && !remainingPiece;
    return result;
}

} // namespace Rules
