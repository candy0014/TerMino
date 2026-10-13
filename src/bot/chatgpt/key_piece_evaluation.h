#include "tetr/util/bit_ops.h"
#ifndef TETR_BOT_CHATGPT_KEY_PIECE_EVALUATION_H
#define TETR_BOT_CHATGPT_KEY_PIECE_EVALUATION_H

#include "evaluation.h"
#include "key_piece_geometry.h"
#include "reachable_holes.h"
#include <limits>

namespace Bot_chatgpt {

// Reserve a particular reachable clear, including its Spin/Mini identity, and
// score the terrain left by that clear. An indispensable piece need not send
// much attack: a Mini which fits a constrained slot and keeps B2B also counts.
// All lookahead credit is terminal value, never actual attack/reward/state.
template<class BaseEvaluation>
class KeyPieceEvaluationWith : public BaseEvaluation {
public:
    static constexpr int horizon = 4;
    static constexpr double defaultHoleWeight = 0.5;
    static constexpr double defaultProjectionWeight = 0.5;
    using Result = typename BaseEvaluation::Result;
    using PhysicalStatus = typename BaseEvaluation::Status;
    struct Status : PhysicalStatus {
        using PhysicalStatus::PhysicalStatus;
        std::uint64_t removedRootRows = 0;
        bool holdEmpty = false;
        Status() = default;
        Status(const PhysicalStatus& value) : PhysicalStatus(value) {}
        Status& operator=(const PhysicalStatus& value) {
            static_cast<PhysicalStatus&>(*this) = value; removedRootRows = 0; holdEmpty = false; return *this;
        }
    };
    struct Projection {
        double value = -std::numeric_limits<double>::infinity();
        double completionBonus = 0;
        Landing key, preparation;
        int lines = 0, steps = 0, attack = 0;
        bool weak = false, discoveredDuringSearch = false;
    };
    struct Statistics {
        std::uint64_t calls = 0, ready = 0, weak = 0, completions = 0,
                      planHits = 0, plans = 0, unsafe = 0, discoveredDuringSearch = 0;
    };
private:
    using Map = KeyPieceGeometry::Map;
    struct Supply { std::string_view next; char hold = ' '; bool canHold = true; };
    struct Choice { char piece, hold; bool swap; int consumed; };
    struct Step { Landing move; Choice choice; };
    struct Plan { std::array<Step, horizon> steps{}; int count = 0; };
    struct PlanEntry {
        KeyPieceGeometry::Rows rows{};
        std::string next;
        Landing key;
        char hold = ' ';
        bool canHold = false, previousClear = false, valid = false;
        std::vector<Plan> plans;
    };
    struct Reserved { const KeyPieceGeometry::Node* key; const KeyPieceGeometry::Node* preparation; bool remappedQuad = false; };
    std::vector<Reserved> reserved_;
    std::vector<const KeyPieceGeometry::Node*> keyTargets_;
    const m_tetris::TetrisContext* context_ = nullptr;
    mutable KeyPieceGeometry geometry_;
    mutable ReachableHoles holes_;
    mutable std::vector<PlanEntry> plans_;
    mutable Statistics stats_;
    mutable Projection last_;
    bool canHold_ = true, cacheEnabled_ = true, dynamicWeakEnabled_ = true;
    double holeWeight_ = defaultHoleWeight, projectionWeight_ = defaultProjectionWeight;

    void adjust_holes(Evaluation::Status& status, const Map& map, Supply supply) const {
        if (!status.dead)
            status.value += holeWeight_ * holes_.evaluate(map, status.previousCleared, supply.next,
                supply.hold, supply.canHold, geometry_, status.risen).adjustment();
    }

    static std::vector<Choice> choices(Supply supply) {
        if (supply.next.empty()) return {};
        const char current = supply.next.front();
        std::vector<Choice> result{{current, supply.hold, false, 1}};
        if (!supply.canHold) return result;
        if (supply.hold != ' ' && supply.hold != current)
            result.push_back({supply.hold, current, true, 1});
        else if (supply.hold == ' ' && supply.next.size() >= 2 && supply.next[1] != current)
            result.push_back({supply.next[1], current, true, 2});
        return result;
    }
    static Supply consume(Supply supply, const Choice& choice) {
        supply.next.remove_prefix(choice.consumed); supply.hold = choice.hold; return supply;
    }
    static std::string families(Supply supply, int window) {
        std::string result;
        if (supply.next.empty()) return result;
        for (char piece : supply.next.substr(0, window))
            if (result.find(piece) == std::string::npos) result += piece;
        if (supply.canHold && supply.hold != ' ' && result.find(supply.hold) == std::string::npos)
            result += supply.hold;
        return result;
    }
    // Geometry proofs are independent of policy, depth and incoming garbage.
    // Their real transitions are replayed with CURRENT values below on every
    // use, so a warm cache cannot freeze an old deadline, combo or attack score.
    std::vector<Plan> plans(const Map& map, Landing key, Supply supply, bool previousClear) const {
        std::size_t hash = key->index * 3 + int(key.spin) + int(key.mini) * 2;
        for (unsigned row : map.row) hash = (hash ^ row) * 1099511628211ull;
        const std::string queue(supply.next.substr(0, horizon + 1));
        for (char c : queue) hash = (hash ^ unsigned(c)) * 1099511628211ull;
        hash ^= unsigned(supply.hold) * 17 + supply.canHold * 2 + previousClear;
        auto& entry = plans_[hash % plans_.size()];
        if (cacheEnabled_ && entry.valid && entry.key == key && entry.next == queue && entry.hold == supply.hold &&
            entry.canHold == supply.canHold && entry.previousClear == previousClear &&
            std::equal(entry.rows.begin(), entry.rows.end(), map.row)) {
            ++stats_.planHits; return entry.plans;
        }
        ++stats_.plans;
        std::vector<Plan> result;
        Map parked = map; Plan prefix; Supply remaining = supply; bool cleared = previousClear;
        const int keyLines = KeyPieceGeometry::lines(map, key.node);
        for (int slot = 0; slot < horizon && !remaining.next.empty(); ++slot) {
            for (const auto& choice : choices(remaining)) if (choice.piece == key->status.t) {
                if (KeyPieceGeometry::lines(parked, key.node) != keyLines || !geometry_.preserves(parked, key, cleared)) continue;
                Plan plan = prefix; plan.steps[plan.count++] = {key, choice}; result.push_back(plan);
            }
            if (!result.empty()) break;
            if (remaining.next.front() == key->status.t || slot + 1 == horizon) break;
            // Hold is reserved. Waiting pieces may neither consume it nor
            // clear/remap the fixed key's cells or change its Spin category.
            struct Parking { Landing move; Map after; double cost; };
            std::vector<Parking> options;
            for (auto move : geometry_.parking(parked, remaining.next.front(), cleared)) {
                Map after = parked; move->attach(context_, after);
                if (KeyPieceGeometry::lines(after, key.node) != keyLines || !key->check(after)) continue;
                const double cost = 100.0 * covered_empty_cells(after) + after.roof * after.roof;
                options.push_back({move, after, cost});
            }
            std::stable_sort(options.begin(), options.end(), [](const Parking& a, const Parking& b) { return a.cost < b.cost; });
            std::optional<Landing> best;
            for (const auto& option : options) if (geometry_.preserves(option.after, key, false)) { best = option.move; break; }
            if (!best) break;
            const Choice choice{remaining.next.front(), remaining.hold, false, 1};
            prefix.steps[prefix.count++] = {*best, choice}; best->node->attach(context_, parked);
            remaining = consume(remaining, choice); cleared = false;
        }
        if (cacheEnabled_) {
            std::copy_n(map.row, 40, entry.rows.begin()); entry.next = queue; entry.key = key;
            entry.hold = supply.hold; entry.canHold = supply.canHold;
            entry.previousClear = previousClear; entry.plans = result; entry.valid = true;
        }
        return result;
    }
    static bool key_clear(const Landing& move, int lines, const Map& before, const Map& after, const Status& status) {
        if (lines <= 0) return false;
        if (move.spin || lines == 4 || after.count == 0) return true;
        // Ordinary repairs are an escape estimate only when no live B2B is
        // being traded away. Merely clearing a row is not a key structure.
        return status.b2b == 0 && covered_empty_cells(before) >= covered_empty_cells(after) + 2;
    }
    Projection replay(const Map& map, const Status& physical, Supply supply, std::size_t depth, const Plan& plan) const {
        Projection result; Map current = map; Status status = physical;
        for (int i = 0; i < plan.count; ++i) {
            const auto& step = plan.steps[i];
            if (supply.next.size() < std::size_t(step.choice.consumed) ||
                !spawn_node(context_, current, supply.next.front(), status.previousCleared) ||
                (step.choice.swap && !spawn_node(context_, current, step.choice.piece, status.previousCleared))) return result;
            Map after = current; const int clear = step.move->attach(context_, after);
            supply = consume(supply, step.choice);
            auto move = step.move;
            const auto ev = BaseEvaluation::eval(move, after, current, clear);
            const m_tetris::TetrisContext::Env env{supply.next.data(), supply.next.size(),
                step.choice.piece, supply.hold, step.choice.swap};
            auto next = BaseEvaluation::get(move, ev, depth + i + 1, status, env);
            if (next.dead || next.risen) { ++stats_.unsafe; return result; }
            if (i + 1 == plan.count) {
                if (!key_clear(move, clear, current, after, status)) return result;
                // The base evaluator recomputes terminal value from the board
                // and physical accumulators. Intermediate hole scores are not
                // consumed by the next transition; only score the endpoint.
                adjust_holes(next, after, supply);
                result.value = next.value; result.lines = clear; result.key = move;
                result.steps = plan.count; result.attack = next.attack - physical.attack;
            }
            current = after; status = next;
        }
        return result;
    }
    const KeyPieceGeometry::Node* mapped(const KeyPieceGeometry::Node* node, std::uint64_t removed) const {
        if (!node) return nullptr;
        const auto footprint = ((std::uint64_t{1} << node->height) - 1) << node->row;
        if (removed & footprint) return nullptr;
        const int shift = Util::popcount64(removed & ((std::uint64_t{1} << node->row) - 1));
        return context_->get(node->status.t, node->status.x, node->status.y - shift, node->status.r);
    }
    Projection ready(const Map& map, const Status& physical, Supply supply, std::size_t depth) const {
        Projection best;
        const auto available = families(supply, horizon + int(supply.canHold && supply.hold == ' '));
        std::vector<const KeyPieceGeometry::Node*> targets;
        for (const auto* original : keyTargets_) if (auto* target = mapped(original, physical.removedRootRows)) targets.push_back(target);
        const auto reservedTargets = targets.size();
        // Clears and placements can expose NEW keys partway through a branch.
        // Discover all currently available special clears on this exact board,
        // including Spin/Mini as well as I quads. Geometry/path caches persist
        // across nodes and actual moves; the score always uses current supply.
        for (char family : available) for (auto key : geometry_.ready(map, family, physical.previousCleared))
            if (std::find(targets.begin(), targets.end(), key.node) == targets.end()) targets.push_back(key.node);
        for (const auto* target : targets) {
            if (!target || available.find(target->status.t) == std::string::npos ||
                !target->check(map) || !KeyPieceGeometry::lines(map, target) ||
                (target->move_down && target->move_down->check(map))) continue;
            const auto found = geometry_.landing(map, target, physical.previousCleared);
            if (!found) continue;
            const auto key = *found;
            Map after = map; const int clear = key->attach(context_, after);
            if (!key_clear(key, clear, map, after, physical)) continue;
            for (const auto& plan : plans(map, key, supply, physical.previousCleared)) {
                auto candidate = replay(map, physical, supply, depth, plan);
                candidate.discoveredDuringSearch = std::find(targets.begin(), targets.begin() + reservedTargets, target)
                    == targets.begin() + reservedTargets;
                if (candidate.value > best.value) best = candidate;
            }
        }
        return best;
    }
    Projection weak(const Map& map, const Status& physical, Supply supply, std::size_t depth) const {
        Projection best;
        for (const auto& choice : choices(supply)) {
            const auto remaining = consume(supply, choice);
            const auto available = families(remaining, horizon - 1 + int(remaining.canHold && remaining.hold == ' '));
            for (const auto& reserve : reserved_) {
                const auto* target = mapped(reserve.key, physical.removedRootRows);
                const auto* prep = mapped(reserve.preparation, physical.removedRootRows);
                if (!prep || prep->status.t != choice.piece || available.find(reserve.key->status.t) == std::string::npos ||
                    !prep->check(map) || (!reserve.remappedQuad && (!target || !target->check(map))) ||
                    (prep->move_down && prep->move_down->check(map))) continue;
                const auto found = geometry_.landing(map, prep, physical.previousCleared); if (!found) continue;
                auto move = *found;
                Map after = map; const int clear = prep->attach(context_, after);
                const KeyPieceGeometry::Node* keyTarget = nullptr;
                if (reserve.remappedQuad) {
                    for (auto* quad : geometry_.quads(after)) if (quad->col == reserve.key->col) { keyTarget = quad; break; }
                } else {
                    int shift = 0;
                    for (int y = prep->row; y < std::min(target->row, prep->row + prep->height); ++y)
                        shift += (map.row[y] | prep->data[y - prep->row]) == 1023u;
                    keyTarget = context_->get(target->status.t, target->status.x, target->status.y - shift, target->status.r);
                }
                const auto key = geometry_.landing(after, keyTarget, clear > 0); if (!key) continue;
                Map residual = after; const int lines = (*key)->attach(context_, residual);
                // Other beam moves may already have completed a cached weak
                // structure. Do not relabel optional parking as preparation.
                bool alreadyReady = false;
                if (reserve.remappedQuad) {
                    for (auto* old : geometry_.quads(map))
                        alreadyReady |= old->col == keyTarget->col && bool(geometry_.landing(map, old, physical.previousCleared));
                } else if (KeyPieceGeometry::lines(map, target) >= lines) {
                    const int wanted = key->spin ? (key->mini ? 1 : 2) : 0;
                    alreadyReady = bool(geometry_.landing(map, target, physical.previousCleared, wanted));
                }
                if (alreadyReady) continue;
                auto ev = BaseEvaluation::eval(move, after, map, clear);
                const m_tetris::TetrisContext::Env env{remaining.next.data(), remaining.next.size(),
                    choice.piece, remaining.hold, choice.swap};
                Status prepared = BaseEvaluation::get(move, ev, depth + 1, physical, env);
                if (prepared.dead || prepared.risen) { ++stats_.unsafe; continue; }
                if (!key_clear(*key, lines, after, residual, prepared)) continue;
                for (const auto& plan : plans(after, *key, remaining, clear > 0)) {
                    if (plan.count + 1 > horizon) continue;
                    auto candidate = replay(after, prepared, remaining, depth + 1, plan);
                    if (!std::isfinite(candidate.value)) continue;
                    candidate.value = physical.value + 0.5 * std::max(0.0, candidate.value - physical.value);
                    candidate.weak = true; candidate.preparation = move; ++candidate.steps;
                    candidate.attack += prepared.attack - physical.attack;
                    if (candidate.value > best.value) best = candidate;
                }
            }
        }
        return best;
    }
    // A real clear can expose a near-complete quad which did not exist at the
    // root. Use the bounded three-row detector here: rescanning every possible
    // Spin roof at every deep node would take time away from the actual beam.
    // Ready Spin/Mini slots of all seven families are still found at each node.
    Projection newly_exposed_weak(const Map& map, const Status& physical, Supply supply, std::size_t depth) const {
        Projection best;
        for (const auto& choice : choices(supply)) {
            const auto remaining = consume(supply, choice);
            if (families(remaining, horizon - 1 + int(remaining.canHold && remaining.hold == ' ')).find('I') != std::string::npos)
                for (const auto& preparation : geometry_.weak(map, choice.piece, 'I', physical.previousCleared, true)) {
                    auto move = preparation.move;
                    Map after = map; const int clear = move->attach(context_, after);
                    const auto ev = BaseEvaluation::eval(move, after, map, clear);
                    const m_tetris::TetrisContext::Env env{remaining.next.data(), remaining.next.size(),
                        choice.piece, remaining.hold, choice.swap};
                    Status prepared = BaseEvaluation::get(move, ev, depth + 1, physical, env);
                    if (prepared.dead || prepared.risen) { ++stats_.unsafe; continue; }
                    Map residual = after;
                    const int lines = preparation.key->attach(context_, residual);
                    if (!key_clear(preparation.key, lines, after, residual, prepared)) continue;
                    for (const auto& plan : plans(after, preparation.key, remaining, clear > 0)) {
                        if (plan.count + 1 > horizon) continue;
                        auto candidate = replay(after, prepared, remaining, depth + 1, plan);
                        if (!std::isfinite(candidate.value)) continue;
                        candidate.value = physical.value + 0.5 * std::max(0.0, candidate.value - physical.value);
                        candidate.weak = true; candidate.preparation = move; ++candidate.steps;
                        candidate.attack += prepared.attack - physical.attack;
                        candidate.discoveredDuringSearch = true;
                        if (candidate.value > best.value) best = candidate;
                    }
                }
        }
        return best;
    }
    bool completed(const Landing& placed, const Result& evaluated, const Status& previous, const Projection& projection) const {
        if (!evaluated.before || !projection.key.node) return false;
        for (const auto& reserve : reserved_) if (reserve.remappedQuad && projection.lines == 4 &&
            reserve.key->col == projection.key->col && mapped(reserve.preparation, previous.removedRootRows) == placed.node) {
            bool alreadyReady = false;
            for (auto* old : geometry_.quads(*evaluated.before))
                alreadyReady |= old->col == projection.key->col && bool(geometry_.landing(*evaluated.before, old, previous.previousCleared));
            if (!alreadyReady) return true;
        }
        // Invert this REAL move's row compaction. A key spanning a removed row
        // has no identical old placement, and cannot claim this completion bonus.
        const auto* key = projection.key.node;
        int first = -1, post = 0;
        for (int pre = 0; pre < 40; ++pre) if (!(evaluated.clearedRows & (std::uint64_t{1} << pre))) {
            if (post == key->row) first = pre;
            if (post >= key->row && post < key->row + key->height && pre - first != post - key->row) return false;
            ++post;
        }
        if (first < 0) return false;
        auto* oldKey = context_->get(key->status.t, key->status.x, key->status.y + first - key->row, key->status.r);
        if (!oldKey || !oldKey->check(*evaluated.before)) return false;
        if (KeyPieceGeometry::lines(*evaluated.before, oldKey) < projection.lines) return true;
        const int wanted = projection.key.spin ? (projection.key.mini ? 1 : 2) : 0;
        return !geometry_.landing(*evaluated.before, oldKey, previous.previousCleared, wanted);
    }
public:
    static m_tetris::TetrisContext::Env projection_environment(m_tetris::TetrisContext::Env env, bool wasEmpty) {
        // The upstream tree consumes empty HOLD correctly, but constructs Env
        // from the parent's iterator. Do not reserve that already-used piece
        // again. This correction is local to key supply, not the base policy.
        if (wasEmpty && env.is_hold && env.length) {
            --env.length; env.next = env.length ? env.next + 1 : nullptr;
        }
        return env;
    }
    void init(const m_tetris::TetrisContext* context) {
        BaseEvaluation::init(context); context_ = context; geometry_.init(context); holes_.init(); plans_.assign(1024, {});
    }
    // Reserve one-preparation structures once per real root. Ready structures
    // are also discovered on each branch's current board. Caches outlive both.
    void begin_search(const Map& map, std::string_view visible, char hold, bool previousClear) {
        reserved_.clear(); keyTargets_.clear();
        if (visible.empty()) return;
        const Supply supply{visible, hold, canHold_};
        // A key at the end of NEXT may come within the four-placement window
        // deeper in the beam. Retain its geometry now, without granting credit
        // before the branch's own supply reaches it.
        const auto available = families(supply, int(visible.size()));
        const auto add = [&](const KeyPieceGeometry::Node* key, const KeyPieceGeometry::Node* prep, bool remapped = false) {
            if (!remapped && std::find(keyTargets_.begin(), keyTargets_.end(), key) == keyTargets_.end()) keyTargets_.push_back(key);
            if (std::none_of(reserved_.begin(), reserved_.end(), [&](const Reserved& r) { return r.key == key && r.preparation == prep && r.remappedQuad == remapped; }))
                reserved_.push_back({key, prep, remapped});
        };
        for (char family : available) for (const auto& key : geometry_.ready(map, family, previousClear)) add(key.node, nullptr);
        if (map.roof > 16) return; // Only the more expensive preparation scan is height-bounded.
        // The current, held, or next visible family can complete a weak slot.
        // Supply/order feasibility is still checked on the actual beam branch.
        for (char prep : families(supply, 2)) for (char family : available)
            for (const auto& plan : geometry_.weak(map, prep, family, previousClear))
                add(plan.originalKey ? plan.originalKey : plan.key.node, plan.move.node, !plan.originalKey);
    }
    std::size_t reserved_count() const { return reserved_.size(); }
    // The adapter may append an exactly deduced singleton to the visible
    // preview. Unordered bag complements never become a fictitious sequence.
    void set_key_state(bool holdEnabled) { canHold_ = holdEnabled; }
    void set_cache_enabled(bool enabled) {
        cacheEnabled_ = enabled; geometry_.set_cache_enabled(enabled); holes_.set_cache_enabled(enabled);
    }
    void set_dynamic_weak_enabled(bool enabled) { dynamicWeakEnabled_ = enabled; }
    // Offline controls can retain the former full-strength scorer. These
    // weights affect terminal value only, never physical attack or garbage.
    void set_feature_weights(double holes, double projection) {
        holeWeight_ = std::clamp(holes, 0.0, 1.0);
        projectionWeight_ = std::clamp(projection, 0.0, 1.0);
    }
    ReachableHoles::Score hole_score(const Map& map, bool previousClear, std::string_view next,
                                   char hold, int risen = 0) const {
        return holes_.evaluate(map, previousClear, next, hold, canHold_, geometry_, risen);
    }
    void set_garbage_forecast(const std::vector<VisiblePacket>& packets, double step) {
        BaseEvaluation::set_garbage_forecast(packets, step); stats_ = {}; geometry_.reset_statistics(); holes_.reset_statistics();
    }
    const Statistics& statistics() const { return stats_; }
    const KeyPieceGeometry::Statistics& geometry_statistics() const { return geometry_.statistics(); }
    const Projection& last_projection() const { return last_; }
    Status get(Landing& landing, const Result& evaluated, std::size_t depth, const Status& previous,
               const m_tetris::TetrisContext::Env& env) const {
        if (previous.dead) { ++stats_.calls; last_ = {}; return previous; }
        Status result = BaseEvaluation::get(landing, evaluated, depth, previous, env);
        result.removedRootRows = previous.removedRootRows;
        result.holdEmpty = previous.holdEmpty && !env.is_hold;
        int post = 0;
        if (evaluated.clearedRows && !reserved_.empty())
        for (int original = 0; original < 40; ++original) if (!(previous.removedRootRows & (std::uint64_t{1} << original))) {
            if (evaluated.clearedRows & (std::uint64_t{1} << post)) result.removedRootRows |= std::uint64_t{1} << original;
            ++post;
        }
        ++stats_.calls; last_ = {};
        const auto& map = *evaluated.shape.map;
        const auto future = projection_environment(env, previous.holdEmpty);
        const Supply supply{future.next ? std::string_view(future.next, future.length) : std::string_view{}, future.hold, canHold_};
        adjust_holes(result, map, supply);
        if (result.dead || result.risen || supply.next.empty() || projectionWeight_ == 0) return result;
        auto best = ready(map, result, supply, depth);
        if (best.value > result.value && completed(landing, evaluated, previous, best)) {
            // Extra credit belongs only to the move which completed the slot.
            // It is bounded, terminal, and disappears on the next real move.
            best.completionBonus = std::min(240.0, 0.1 * (best.value - result.value));
            best.value += best.completionBonus;
        }
        if (depth <= 2 && map.roof <= 16 && best.value <= result.value) {
            const auto setup = weak(map, result, supply, depth);
            if (setup.value > best.value) best = setup;
        }
        if (dynamicWeakEnabled_ && evaluated.shape.clear && map.roof <= 16 && best.value <= result.value) {
            const auto setup = newly_exposed_weak(map, result, supply, depth);
            if (setup.value > best.value) best = setup;
        }
        if (best.value > result.value) {
            best.value = result.value + projectionWeight_ * (best.value - result.value);
            best.completionBonus *= projectionWeight_;
            result.value = best.value; last_ = best;
            if (best.weak) ++stats_.weak; else ++stats_.ready;
            stats_.completions += best.completionBonus > 0;
            stats_.discoveredDuringSearch += best.discoveredDuringSearch;
        }
        return result;
    }
};
using KeyPieceEvaluation = KeyPieceEvaluationWith<Evaluation>;
}
#endif
