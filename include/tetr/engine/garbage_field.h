#ifndef TETR_ENGINE_GARBAGE_FIELD_H
#define TETR_ENGINE_GARBAGE_FIELD_H

#include "tetr/engine/field_mutation.h"

#include <functional>
#include <vector>

namespace Engine {

struct GarbageShiftResult {
    std::vector<CellChange> changes;
};

GarbageShiftResult shift_field_up_for_garbage(
    int width,
    int firstRow,
    int bottomExclusive,
    int riseLines,
    const CellValue& cellValue,
    const CopyRow& copyRow,
    bool collectChanges = true);

std::vector<CellChange> write_generated_row(
    int row,
    const std::vector<Runtime::BoardCell>& values,
    const CellValue& cellValue,
    const SetCell& setCell,
    bool collectChanges = true);

} // namespace Engine

#endif
