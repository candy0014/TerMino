#ifndef TETR_BOT_CHATGPT_FIXED_ROOT_SEARCH_H
#define TETR_BOT_CHATGPT_FIXED_ROOT_SEARCH_H
#include <stdexcept>

namespace Bot_chatgpt {
// Offline only. The callback must call run[_hold](map, start, ..., 0).
// Upstream parameterless run[_hold]() does not expand a fresh root whose
// identity is null. Repeating the full overload retains the same root tree.
template<class Engine, class Pass>
auto fixed_root_passes(Engine& engine, Pass pass, int iterations) {
    if (iterations < 1) throw std::runtime_error("Positive pass count required");
    auto best = pass();
    for (int i = 1; i < iterations; ++i) {
        if (engine.memory_usage() >= (256ull << 20))
            throw std::runtime_error("Fixed-root memory cap reached before pass budget");
        best = pass();
    }
    return best;
}
}
#endif
