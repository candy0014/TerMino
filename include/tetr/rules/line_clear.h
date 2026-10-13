#ifndef TETR_RULES_LINE_CLEAR_H
#define TETR_RULES_LINE_CLEAR_H

#include <vector>

namespace Rules {

struct RowMove {
    int destination = 0;
    int source = 0;
};

// Reproduces the legacy field-collapse mapping. Rows are listed bottom-up in
// the same order in which place.h used to copy them.
std::vector<RowMove> build_gravity_collapse_plan(
    int firstRow,
    int endRow,
    const std::vector<int>& clearedRows);

} // namespace Rules

#endif
