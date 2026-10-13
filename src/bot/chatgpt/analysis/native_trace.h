#ifndef TETR_BOT_CHATGPT_ANALYSIS_NATIVE_TRACE_H
#define TETR_BOT_CHATGPT_ANALYSIS_NATIVE_TRACE_H
// Offline instrumentation only. No score, ordering, geometry or pruning changes.
#include "../root_snapshot.h"
#include "rule_tetr_srs_plus.h"

namespace Bot_chatgpt::Analysis {
class NativeTrace : public Evaluation {
public:
    struct Status : Evaluation::Status {
        std::size_t trace = 0;
        int cursor = 0, held = -2;
    };
    struct Record {
        std::size_t parent = 0, createdPass = 0, expandedPass = 0;
        Landing landing;
        Status status;
        int depth = 0, lines = 0, roof = 0, holes = 0;
        bool hold = false, nextMismatch = false, hiddenActive = false;
    };
    std::size_t pass = 0;
    const RootSnapshot* root = nullptr;
    mutable std::vector<Record> records{Record{}};

    Status get(Landing& landing, const Result& evaluated, std::size_t depth,
               const Status& previous, const m_tetris::TetrisContext::Env& env) const {
        Status result;
        static_cast<Evaluation::Status&>(result) = Evaluation::get(landing, evaluated, depth, previous, env);
        result.trace = records.size();
        result.cursor = previous.cursor + 1;
        result.held = previous.held;
        const bool hidden = previous.cursor >= int(root->next.size());
        if (env.is_hold && !hidden) {
            if (previous.held == -1) ++result.cursor;
            result.held = root->next[previous.cursor];
        }
        const bool knownNext = result.cursor < int(root->next.size());
        const bool mismatch = !hidden && (knownNext
            ? !env.length || piece_index(*env.next) != root->next[result.cursor]
            : env.length != 0);
        if (!records[previous.trace].expandedPass) records[previous.trace].expandedPass = pass;
        records.push_back({previous.trace, pass, 0, landing, result, int(depth) + 1,
            evaluated.shape.clear, evaluated.shape.map->roof, covered_empty_cells(*evaluated.shape.map),
            env.is_hold, mismatch, hidden});
        return result;
    }

    std::vector<std::size_t> path(std::size_t leaf) const {
        std::vector<std::size_t> result;
        while (leaf) {
            if (leaf >= records.size()) throw std::runtime_error("Invalid trace parent");
            result.push_back(leaf); leaf = records[leaf].parent;
        }
        std::reverse(result.begin(), result.end());
        return result;
    }
    std::size_t sibling_rank(std::size_t id) const {
        const auto& node = records.at(id);
        std::size_t rank = 1;
        for (const auto& other : records)
            if (other.status.trace && other.parent == node.parent && other.status.value > node.status.value)
                ++rank;
        return rank; // Strictly better siblings, ties share a rank.
    }
};

using PlainEngine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, Evaluation, Search>;
using TracedEngine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, NativeTrace, Search>;

template<class Engine>
void configure_native(Engine& engine, const RootSnapshot& root) {
    static_cast<Evaluation::Status&>(*engine.status()) = root.status;
    engine.ai()->set_garbage_forecast(root.packets, root.step);
    engine.search_config()->rootPreviousCleared = root.status.previousCleared;
    engine.memory_limit(256ull << 20);
}

template<class Engine>
auto native_pass(Engine& engine, const RootSnapshot& root, int milliseconds = 0) {
    constexpr char types[] = "IJLOSTZ";
    const auto map = BotNativeHost::make_map(root.field);
    std::string preview;
    for (std::size_t i = 1; i < root.next.size(); ++i) preview += types[root.next[i]];
    const auto* start = native_pose(engine.context().get(), types[root.next[0]], root.pose);
    if (!start || !start->check(map)) throw std::runtime_error("Invalid actual start");
    return root.hold != -2
        ? engine.run_hold(map, start, root.hold >= 0 ? types[root.hold] : ' ', true,
                          preview.data(), preview.size(), milliseconds)
        : engine.run(map, start, preview.data(), preview.size(), milliseconds);
}

template<class Result>
bool first_legal(const m_tetris::TetrisContext* context, const RootSnapshot& root, const Result& best) {
    if (!best.target.node) return false;
    constexpr char types[] = "IJLOSTZ";
    const int selected = piece_index(best.target->status.t);
    const int expected = best.change_hold ? (root.hold >= 0 ? root.hold :
        (root.next.size() > 1 ? root.next[1] : -1)) : root.next[0];
    if (selected != expected) return false;
    const bool swap = best.change_hold && selected != root.next[0];
    const auto map = BotNativeHost::make_map(root.field);
    const auto* start = swap ? spawn_node(context, map, types[selected], root.status.previousCleared)
        : native_pose(context, types[selected], root.pose);
    Piece::Pose answer; int kick = 0;
    return start && BotNativeHost::reachable(best.target.node,
        Piece::get_definition(Piece::Type::from_index(selected)), root.field,
        host_pose(*start), best.target.spin, best.target.mini, answer, kick);
}
}
#endif
