#ifndef TETR_BOT_CHATGPT_ANALYSIS_SPIN_PATHS_H
#define TETR_BOT_CHATGPT_ANALYSIS_SPIN_PATHS_H
// Offline only: bounded counterfactual search, never linked into playing policy.
#include "../root_snapshot.h"
#include <array>
#include <optional>
#include <set>

namespace Bot_chatgpt::Analysis {
constexpr char types[] = "IJLOSTZ";

// Full graph BFS from the actual spawn. Deliberately no skyline shortcut:
// the production shortcut has a documented placement-coverage limitation.
// Keep normal, mini and full-spin variants, not only the strongest variant.
class Moves {
    const m_tetris::TetrisContext* context_;
    std::vector<unsigned> visited_, landed_;
    unsigned generation_ = 0;
    std::vector<const m_tetris::TetrisNode*> queue_;
    std::vector<Landing> result_;
public:
    explicit Moves(const m_tetris::TetrisContext* context) : context_(context),
        visited_(context->node_max()), landed_(3 * context->node_max()) {}
    const std::vector<Landing>& enumerate(const m_tetris::TetrisMap& map,
                                         const m_tetris::TetrisNode* start) {
        queue_.clear(); result_.clear();
        if (!start || !start->check(map)) return result_;
        if (++generation_ == 0) {
            std::fill(visited_.begin(), visited_.end(), 0);
            std::fill(landed_.begin(), landed_.end(), 0);
            ++generation_;
        }
        m_tetris::TetrisMapSnap snap;
        start->build_snap(map, context_, snap);
        const auto enqueue = [&](const m_tetris::TetrisNode* node, int kick) {
            if (!node || !node->check(snap)) return;
            if (!node->move_down || !node->move_down->check(snap)) {
                Landing landing(node); landing.kick = kick;
                if (kick) {
                    Rules::SpinInput input;
                    input.pose = host_pose(*node);
                    input.spinChecker = node->status.t == 'T' ? Piece::SpinCheck::TCorner : Piece::SpinCheck::Immobility;
                    input.lastRotation = Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(kick - 1));
                    input.mapWidth = 10; input.mapHeight = 20;
                    input.immobile = (!node->move_up || !node->move_up->check(snap)) &&
                        (!node->move_left || !node->move_left->check(snap)) &&
                        (!node->move_right || !node->move_right->check(snap));
                    const auto spin = Rules::detect_spin(input, [&](int row, int col) {
                        const int y = 19 - row;
                        return y >= 0 && y < 40 && col >= 0 && col < 10 && map.full(col, y);
                    });
                    landing.spin = spin.spin; landing.mini = spin.mini;
                }
                const unsigned kind = landing.spin ? (landing.mini ? 1 : 2) : 0;
                const auto key = node->index * 3 + kind;
                if (landed_[key] != generation_) {
                    landed_[key] = generation_;
                    result_.push_back(landing);
                }
            }
            if (visited_[node->index] != generation_) {
                visited_[node->index] = generation_; queue_.push_back(node);
            }
        };
        enqueue(start, 0);
        for (std::size_t i = 0; i < queue_.size(); ++i) {
            const auto* node = queue_[i];
            enqueue(node->move_down, 0); enqueue(node->move_left, 0); enqueue(node->move_right, 0);
            for (const auto* kicks : {node->wall_kick_clockwise, node->wall_kick_counterclockwise, node->wall_kick_opposite})
                for (int k = 0; k < m_tetris::max_wall_kick && kicks[k]; ++k) {
                    if (!kicks[k]->check(snap)) continue;
                    enqueue(kicks[k], k + 1); break;
                }
        }
        return result_;
    }
};

struct LiveMove { int piece, row, col, rotation, spin, mini, lines, attack, b2b; };
struct Choice { int piece, cursor, hold; bool swap; };
inline std::vector<Choice> choices(const RootSnapshot& root, int cursor, int held) {
    if (cursor >= int(root.next.size())) return {};
    const int current = root.next[cursor];
    std::vector<Choice> out{{current, cursor + 1, held, false}};
    if (held >= 0 && held != current) out.push_back({held, cursor + 1, current, true});
    if (held == -1 && cursor + 1 < int(root.next.size()) && root.next[cursor + 1] != current)
        out.push_back({root.next[cursor + 1], cursor + 2, current, true});
    // Same-family HOLD cannot be encoded by the host's returned piece ID.
    return out;
}
struct Step {
    Landing landing;
    Choice choice;
    int lines = 0, attack = 0, b2b = 0, combo = 0, incoming = 0, roof = 0, holes = 0;
    double value = 0, reward = 0, risk = 0;
    bool productionGeometry = false, productionSpin = false;
};
struct State {
    m_tetris::TetrisMap map{10, 40};
    Evaluation::Status status;
    int cursor = 0, hold = -2, spins = 0, peakRoof = 0;
    bool broken = false;
    std::vector<Step> path;
};
struct Work {
    std::uint64_t expanded = 0, evaluated = 0, brokenCuts = 0, unknownRiseCuts = 0,
                  deadCuts = 0, previewCuts = 0, duplicates = 0, beamCuts = 0;
};
struct Result {
    std::optional<State> best, twoSpins, prefix;
    Work work;
    bool liveMatched = false;
    int reachedDepth = 0;
};
enum class Mode { Free, LiveFirst, Preserve };

inline bool matches(const Landing& landing, const LiveMove& live) {
    const auto pose = host_pose(*landing.node);
    return piece_index(landing->status.t) == live.piece && pose.position.row == live.row &&
        pose.position.col == live.col && pose.rotation.index() == live.rotation &&
        int(landing.spin) == live.spin && int(landing.mini) == live.mini;
}

class SpinPaths {
    const m_tetris::TetrisContext* context_;
    Moves moves_;
    Evaluation evaluation_;
    const m_tetris::TetrisNode* start(const RootSnapshot& root, const State& state, const Choice& choice) const {
        return state.path.empty() && !choice.swap
            ? native_pose(context_, types[choice.piece], root.pose)
            : spawn_node(context_, state.map, types[choice.piece], state.status.previousCleared);
    }
    State advance(const RootSnapshot& root, const State& parent, Choice choice, Landing landing) {
        State child = parent;
        const int lines = landing->attach(context_, child.map);
        const auto evaluated = evaluation_.eval(landing, child.map, parent.map, lines);
        std::string next;
        for (int i = choice.cursor; i < int(root.next.size()); ++i) next += types[root.next[i]];
        // Explicitly advance the visible queue, including the extra piece
        // consumed by empty HOLD. No hidden replacement piece is supplied.
        const m_tetris::TetrisContext::Env env{next.data(), next.size(), types[choice.piece],
            choice.hold >= 0 ? types[choice.hold] : ' ', choice.swap};
        child.status = evaluation_.get(landing, evaluated, parent.path.size(), parent.status, env);
        if (next.empty() && !child.status.risen) {
            // End of visible preview: certify spawn safety for every possible
            // next family instead of assuming a convenient hidden piece.
            for (char type : std::string("IJLOSTZ"))
                if (!spawn_node(context_, child.map, type, lines > 0)) child.status.dead = true;
        }
        child.cursor = choice.cursor; child.hold = choice.hold;
        child.spins += landing.spin && lines > 0;
        child.broken = parent.broken || (parent.status.b2b > 0 && child.status.b2b == 0);
        child.peakRoof = std::max(parent.peakRoof, child.map.roof);
        child.path.push_back({landing, choice, lines, child.status.attack - parent.status.attack,
            child.status.b2b, child.status.combo, child.status.incoming, child.map.roof,
            covered_empty_cells(child.map), child.status.value, child.status.reward, child.status.pathRisk});
        return child;
    }
    static auto key(const State& s) {
        std::array<int, 51> key{};
        for (int y = 0; y < 40; ++y) key[y] = s.map.row[y];
        key[40] = s.cursor; key[41] = s.hold; key[42] = s.status.b2b;
        key[43] = s.status.combo; key[44] = s.status.incoming;
        key[45] = s.status.readyIncoming; key[46] = s.status.garbageHeight;
        key[47] = s.status.previousCleared; key[48] = std::min(2, s.spins);
        key[49] = s.broken; key[50] = s.peakRoof;
        return key;
    }
public:
    explicit SpinPaths(const m_tetris::TetrisContext* context) : context_(context), moves_(context) {
        evaluation_.init(context);
    }
    Result run(const RootSnapshot& root, const LiveMove& live, int depth, int width, Mode mode) {
        if (depth < 1 || depth > 7 || width < 1 || width > 4096 || root.status.b2b <= 0)
            throw std::runtime_error("Invalid counterfactual limits or inactive root chain");
        evaluation_.set_garbage_forecast(root.packets, root.step);
        State initial; initial.map = BotNativeHost::make_map(root.field);
        initial.status = root.status; initial.hold = root.hold; initial.peakRoof = initial.map.roof;
        std::vector<State> beam{initial};
        Result result;
        for (int d = 0; d < depth && !beam.empty(); ++d) {
            std::vector<State> generated;
            for (const auto& parent : beam) {
                ++result.work.expanded;
                const auto options = choices(root, parent.cursor, parent.hold);
                if (options.empty()) ++result.work.previewCuts;
                for (const auto choice : options) {
                    for (const auto& landing : moves_.enumerate(parent.map, start(root, parent, choice))) {
                        if (mode == Mode::LiveFirst && d == 0 && !matches(landing, live)) continue;
                        State child = advance(root, parent, choice, landing);
                        ++result.work.evaluated;
                        if (mode == Mode::LiveFirst && d == 0) {
                            const auto& first = child.path.front();
                            if (first.lines != live.lines || first.attack != live.attack || first.b2b != live.b2b)
                                throw std::runtime_error("Recorded first move disagrees with host rules");
                            result.liveMatched = true;
                        }
                        if (child.status.risen) { ++result.work.unknownRiseCuts; continue; }
                        if (child.status.dead) { ++result.work.deadCuts; continue; }
                        if (mode == Mode::Preserve && child.broken) { ++result.work.brokenCuts; continue; }
                        generated.push_back(std::move(child));
                    }
                }
            }
            std::stable_sort(generated.begin(), generated.end(), [](const State& a, const State& b) {
                return a.status.value > b.status.value;
            });
            beam.clear();
            std::array<int, 3> buckets{};
            std::set<std::array<int, 51>> seen;
            for (auto& child : generated) {
                const int bucket = std::min(2, child.spins);
                if (buckets[bucket] >= width) { ++result.work.beamCuts; continue; }
                if (!seen.insert(key(child)).second) { ++result.work.duplicates; continue; }
                ++buckets[bucket]; beam.push_back(std::move(child));
            }
            if (!beam.empty()) result.reachedDepth = d + 1;
            for (const auto& state : beam) {
                if (!state.broken && state.spins >= 2 && (!result.prefix ||
                    state.path.size() > result.prefix->path.size() ||
                    (state.path.size() == result.prefix->path.size() && state.status.value > result.prefix->status.value)))
                    result.prefix = state;
                if (d + 1 != depth) continue;
                if (!result.best || state.status.value > result.best->status.value) result.best = state;
                if (!state.broken && state.spins >= 2 &&
                    (!result.twoSpins || state.status.value > result.twoSpins->status.value)) result.twoSpins = state;
            }
        }
        if (mode == Mode::LiveFirst && !result.liveMatched)
            throw std::runtime_error("Recorded first move absent from full graph enumeration");
        return result;
    }

    // Independent host BFS for every reported placement, then replay the exact
    // queue/chain/garbage transition. No continuation after unknown rises.
    void verify(const RootSnapshot& root, State& witness) {
        evaluation_.set_garbage_forecast(root.packets, root.step);
        Search production;
        Search::Config config{root.status.previousCleared};
        production.init(context_, &config);
        State state; state.map = BotNativeHost::make_map(root.field); state.status = root.status;
        state.hold = root.hold; state.peakRoof = state.map.roof;
        for (auto& step : witness.path) {
            bool choiceValid = false;
            for (const auto& c : choices(root, state.cursor, state.hold))
                choiceValid |= c.piece == step.choice.piece && c.cursor == step.choice.cursor &&
                    c.hold == step.choice.hold && c.swap == step.choice.swap;
            if (!choiceValid) throw std::runtime_error("Invalid witness HOLD / preview consumption");
            Runtime::Playfield field; field.initialize(10, 20); field.clear();
            for (int y = 0; y < 40; ++y)
                for (int x = 0; x < 10; ++x)
                    if (state.map.row[y] & (1u << x)) field.set_cell(19 - y, x, Runtime::BoardCell::garbage());
            const auto* origin = start(root, state, step.choice);
            Piece::Pose answer; int kick = 0;
            if (!origin || !BotNativeHost::reachable(step.landing.node,
                Piece::get_definition(Piece::Type::from_index(step.choice.piece)), field,
                host_pose(*origin), step.landing.spin, step.landing.mini, answer, kick))
                throw std::runtime_error("Witness not reachable by host rules");
            for (const auto& candidate : *production.search(state.map, origin, state.path.size())) {
                if (candidate->index_filtered != step.landing->index_filtered) continue;
                step.productionGeometry = true;
                step.productionSpin |= candidate.spin == step.landing.spin && candidate.mini == step.landing.mini;
            }
            state = advance(root, state, step.choice, step.landing);
            const auto& replay = state.path.back();
            if (state.status.risen || state.status.dead || replay.lines != step.lines ||
                replay.attack != step.attack || replay.b2b != step.b2b || replay.value != step.value)
                throw std::runtime_error("Witness transition mismatch / censored continuation");
        }
        if (!(state.map == witness.map) || state.status.value != witness.status.value)
            throw std::runtime_error("Witness terminal mismatch");
    }
};
}
#endif
