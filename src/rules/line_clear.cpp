#include "tetr/rules/line_clear.h"

#include <algorithm>
#include <unordered_set>

namespace Rules {

std::vector<RowMove> build_gravity_collapse_plan(
    int firstRow,
    int endRow,
    const std::vector<int>& clearedRows) {
    std::unordered_set<int> cleared(clearedRows.begin(), clearedRows.end());
    std::vector<RowMove> moves;

    int source = endRow;
    bool shifted = false;
    for (int destination = endRow - 1; destination >= firstRow; --destination) {
        --source;
        while (source >= firstRow + 1 && cleared.count(source) != 0) {
            --source;
            shifted = true;
        }
        source = std::max(source, firstRow + 1);
        if (!shifted) continue;
        moves.push_back(RowMove{destination, source});
    }
    return moves;
}

} // namespace Rules
