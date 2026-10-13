// C++ adaptation of bot/src/dag.rs and modes/normal.rs, MinusKelvin/cold-clear.
// SPDX-License-Identifier: MPL-2.0
#include "core.h"
#include <algorithm>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>

namespace ColdClear {
namespace {
struct Edge { std::size_t node; Move move; Reward reward; Rules::PlacementResult lock; };
struct Node {
    State state;
    Value evaluation;
    int depth = 0;
    bool expanded = false;
    bool death = false;
    bool terminal = false;
    std::vector<std::vector<Edge>> groups;
    std::vector<std::size_t> parents;
};

template<class T> void append(std::string& key, const T& value) {
    key.append(reinterpret_cast<const char*>(&value),sizeof(value));
}
std::string state_key(const State& state, int depth, std::size_t visible, bool halfClear) {
    std::string key;
    key.reserve(400);
    append(key,depth);
    for (auto row : state.board.rows) append(key,row);
    if (halfClear) for (auto row : state.board.pieceRows) append(key,row);
    append(key,state.cursor); append(key,state.hold); append(key,state.bag);
    append(key,state.b2b); append(key,state.combo); append(key,state.garbageHeight);
    append(key,state.previousCleared); append(key,state.rise);
    if (!state.incoming.empty()) {
        append(key,state.time); append(key,state.placed); append(key,state.sent);
        append(key,state.cancellation.cancellationFraction); append(key,state.cancellation.outgoingFraction);
    }
    append(key,state.incoming.size());
    for (auto packet : state.incoming) {
        append(key,packet.line_count()); append(key,packet.arrival_time().seconds()); append(key,packet.preserve_hole());
    }
    // Consumed speculative pieces do not distinguish equivalent boards.
    const int begin = std::max(0,state.cursor-static_cast<int>(visible));
    for (int i = begin; i < static_cast<int>(state.tail.size()); ++i) append(key,state.tail[i]);
    return key;
}
int queued(const Input& input, const State& state, int offset = 0) {
    const int index = state.cursor + offset;
    if (index < static_cast<int>(input.next.size())) return input.next[index];
    const int tail = index - static_cast<int>(input.next.size());
    return tail >= 0 && tail < static_cast<int>(state.tail.size()) ? state.tail[tail] : -1;
}
std::vector<State> chance_cases(const Input& input, State state, bool allowHold) {
    const bool needNext = queued(input,state) < 0;
    const bool needHold = allowHold && state.hold < 0 && queued(input,state,1) < 0;
    if (!needNext && !needHold) return {std::move(state)};
    std::vector<State> cases;
    const unsigned possible = input.environment.randomPieces ? 127 : state.bag ? state.bag : 127;
    for (int piece = 0; piece < 7; ++piece) if (possible & (1u << piece)) {
        State next = state;
        next.tail.push_back(piece);
        if (!input.environment.randomPieces) {
            next.bag = possible & ~(1u << piece);
            if (!next.bag) next.bag = 127;
        }
        auto children = chance_cases(input,std::move(next),allowHold);
        for (auto& child : children) cases.push_back(std::move(child));
    }
    return cases;
}

class Dag {
public:
    Dag(const Input& input, Bot_coldclear::Diagnostics& diagnostics)
        : input_(input), diagnostics_(diagnostics), random_(seed(input.root)) {
        Node root; root.state = input.root;
        nodes_.push_back(std::move(root));
    }

    Child run() {
        const int width = std::clamp(input_.environment.autoplaySearchWidth,1,1000);
        const std::size_t budget = std::max(256,width*24);
        // Finish whole expansions/chance groups so the expectation is unbiased.
        // A last expansion may exceed the budget by one complete child list.
        expand(0);
        for (int cycle = 0; cycle < width*8 && nodes_.size() < budget; ++cycle) {
            const auto leaf = select_leaf();
            if (leaf == Invalid) continue;
            expand(leaf);
        }
        diagnostics_.nodes = nodes_.size();
        Child result;
        const auto& groups = nodes_[0].groups;
        if (groups.empty()) return result;
        const Edge* backup = nullptr;
        const Edge* picked = nullptr;
        // Root always uses only public pieces, so it has one known group.
        for (const auto& edge : groups.front()) {
            if (nodes_[edge.node].death) continue;
            const auto& child = nodes_[edge.node];
            const auto h = child.state.board.heights();
            bool safe = true;
            // Original Standard::pick_move defense, with the host cancellation
            // and cap applied before measuring the unknown garbage rise.
            for (int x = 3; x <= 5; ++x) if (h[x] + child.state.rise > Height) safe = false;
            if (safe) { picked = &edge; break; }
            if (!backup || edge_value(*backup).spike < edge_value(edge).spike) backup = &edge;
        }
        if (!picked) picked = backup;
        if (!picked) return result;
        result.state = nodes_[picked->node].state;
        result.move = picked->move;
        result.lock = picked->lock;
        result.evaluation = {nodes_[picked->node].evaluation,picked->reward};
        return result;
    }

private:
    static constexpr std::size_t Invalid = std::numeric_limits<std::size_t>::max();
    const Input& input_;
    Bot_coldclear::Diagnostics& diagnostics_;
    std::vector<Node> nodes_;
    std::unordered_map<std::string,std::size_t> deduplicator_;
    std::mt19937 random_;

    static std::uint32_t seed(const State& root) {
        std::uint32_t result = 0xC01DC1EA;
        for (auto row : root.board.rows) result = (result ^ row) * 16777619u;
        return result ^ static_cast<unsigned>(root.placed);
    }
    Value edge_value(const Edge& edge) const { return nodes_[edge.node].evaluation.add(edge.reward); }

    std::size_t select_leaf() {
        std::size_t id = 0;
        for (;;) {
            const auto& node = nodes_[id];
            if (node.death || node.terminal) return Invalid;
            if (!node.expanded) return id;
            if (node.groups.empty()) return Invalid;
            const auto& group = node.groups[random_() % node.groups.size()];
            if (group.empty()) return Invalid;
            Value minimum;
            bool found = false;
            for (auto i = group.rbegin(); i != group.rend(); ++i)
                if (!nodes_[i->node].death) { minimum = edge_value(*i); found = true; break; }
            if (!found) return Invalid;
            std::vector<double> weights;
            weights.reserve(group.size());
            for (std::size_t rank = 0; rank < group.size(); ++rank) {
                const auto& edge = group[rank];
                weights.push_back(nodes_[edge.node].death ? 0 : static_cast<double>(edge_value(edge).weight(minimum,rank)));
            }
            std::discrete_distribution<std::size_t> sampler(weights.begin(),weights.end());
            id = group[sampler(random_)].node;
        }
    }

    void expand(std::size_t id) {
        const State state = nodes_[id].state;
        const int depth = nodes_[id].depth;
        nodes_[id].expanded = true;
        const bool allowHold = input_.canHold &&
            !(depth == 0 && state.hold < 0 && input_.next.size() < 2);
        const auto cases = chance_cases(input_,state,allowHold);
        if (cases.size() > 1) ++diagnostics_.speculations;
        std::vector<std::vector<Edge>> groups;
        for (const auto& branch : cases) {
            std::vector<Edge> children;
            const int current = queued(input_,branch);
            auto add = [&](int selected, bool useHold) {
                State advanced = branch;
                ++advanced.cursor;
                if (useHold) {
                    advanced.hold = current;
                    if (branch.hold < 0) ++advanced.cursor;
                }
                // HOLD respawns the selected piece; an ordinary root keeps the
                // actual active pose, including midair starts and clutch lifts.
                const auto start = depth == 0 && !useHold ? input_.start : spawn(input_,branch,selected);
                for (auto move : find_moves(input_,branch,selected,start,useHold)) {
                    auto child = lock_move(input_,advanced,move);
                    const auto key = state_key(child.state,depth+1,input_.next.size(),input_.environment.attackRules.halfClearEnabled);
                    auto found = deduplicator_.find(key);
                    std::size_t childId;
                    if (found != deduplicator_.end()) {
                        childId = found->second;
                        ++diagnostics_.transpositions;
                    } else {
                        childId = nodes_.size();
                        Node node; node.state = std::move(child.state);
                        node.evaluation = child.evaluation.value; node.depth = depth+1;
                        node.terminal = node.state.rise > 0 || node.depth >= std::min(16,static_cast<int>(input_.next.size())+2);
                        nodes_.push_back(std::move(node));
                        deduplicator_.emplace(key,childId);
                        diagnostics_.depth = std::max(diagnostics_.depth,depth+1);
                    }
                    auto& parents = nodes_[childId].parents;
                    if (std::find(parents.begin(),parents.end(),id) == parents.end()) parents.push_back(id);
                    children.push_back({childId,move,child.evaluation.reward,std::move(child.lock)});
                }
            };
            if (current >= 0 && current < 7) {
                add(current,false);
                if (allowHold) {
                    const int held = branch.hold >= 0 ? branch.hold : queued(input_,branch,1);
                    if (held >= 0 && held < 7 && held != current) add(held,true);
                }
            }
            groups.push_back(std::move(children));
        }
        nodes_[id].groups = std::move(groups);
        backpropagate(id);
    }

    void backpropagate(std::size_t id) {
        // All parents are in the preceding depth. Process breadth-first and
        // merge duplicate parents so DAG updates reach every route exactly once.
        std::vector<std::size_t> pending{id};
        while (!pending.empty()) {
            std::vector<std::size_t> next;
            for (auto current : pending) {
                auto& node = nodes_[current];
                if (!node.expanded) continue;
                const Value old = node.evaluation;
                const bool oldDeath = node.death;
                Value total{}, worst{};
                int live = 0, deaths = 0;
                for (auto& group : node.groups) {
                    std::stable_sort(group.begin(),group.end(),[&](const Edge& a,const Edge& b) {
                        if (nodes_[a.node].death != nodes_[b.node].death) return !nodes_[a.node].death;
                        return edge_value(b) < edge_value(a);
                    });
                    Value best;
                    bool found = false;
                    for (const auto& edge : group) if (!nodes_[edge.node].death) {
                        if (!found) { best = edge_value(edge); found = true; }
                        else best.improve(edge_value(edge));
                    }
                    if (!found) { ++deaths; continue; }
                    if (live == 0 || best < worst) worst = best;
                    total.value += best.value; total.spike += best.spike; ++live;
                }
                node.death = live == 0;
                if (live) {
                    const int count = live+deaths;
                    node.evaluation = {(total.value + (worst.value-1000)*deaths)/count,total.spike/count};
                }
                if (!(node.evaluation == old) || node.death != oldDeath)
                    for (auto parent : node.parents) if (std::find(next.begin(),next.end(),parent) == next.end()) next.push_back(parent);
            }
            pending = std::move(next);
        }
    }
};
}
Child search(const Input& input, Bot_coldclear::Diagnostics& diagnostics) { return Dag(input,diagnostics).run(); }
}
