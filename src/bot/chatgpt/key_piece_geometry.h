#include "tetr/util/bit_ops.h"
#ifndef TETR_BOT_CHATGPT_KEY_PIECE_GEOMETRY_H
#define TETR_BOT_CHATGPT_KEY_PIECE_GEOMETRY_H

#include "search.h"
#include <array>
#include <optional>

namespace Bot_chatgpt {

// Geometry only. This object lives in the persistent thread-local Workspace,
// across search.update() and real placements. Neither scores nor queue/chain/
// garbage state are cached. Reinitializing the context invalidates all pointers.
class KeyPieceGeometry {
public:
    using Map = m_tetris::TetrisMap;
    using Node = m_tetris::TetrisNode;
    using Rows = std::array<unsigned, 40>;
    struct Statistics {
        std::uint64_t boardHits = 0, pathHits = 0, pathMisses = 0, enumerations = 0,
                      targets = 0, weakHits = 0, bfs = 0, fillComponentHits = 0,
                      skyHits = 0, skyBuilds = 0;
    };
    struct Preparation { Landing move, key; int lines = 0; const Node* originalKey = nullptr; };
private:
    struct Witness {
        const Node* start = nullptr;
        Landing target;
        int kind = -1;
        Rows empty{}, occupied{};
    };
    struct PathEntry {
        Witness proof;
        Rows failedBoard{};
        const Node* failedStart = nullptr;
        const Node* failedTarget = nullptr;
        int failedKind = -1;
    };
    struct BoardEntry {
        Rows rows{};
        const Node* start = nullptr;
        bool valid = false;
        std::vector<Landing> keys;
    };
    struct WeakEntry {
        Rows rows{};
        char prepare = ' ', key = ' ';
        bool previousClear = false, quadOnly = false, valid = false;
        std::vector<Preparation> plans;
    };
    // The reverse graph asks whether many different poses can be reached by
    // the SAME rotate/translate/drop prefix. Build those vertical intervals
    // once; only a successful query needs to materialize its full witness.
    struct SkyRoute { Witness prefix; const Node* top; const Node* bottom; int kick; };
    struct SkyColumn { int rotation, column; std::vector<SkyRoute> routes; };
    struct SkyEntry {
        Rows rows{};
        const Node* start = nullptr;
        bool valid = false;
        std::vector<SkyRoute> orientations;
        std::vector<SkyColumn> columns;
    };
    const m_tetris::TetrisContext* context_ = nullptr;
    std::array<std::array<const Node*, 4>, 7> anchors_{};
    // Shape-to-cell alignment is independent of the board. Preserve the old
    // rotation/cell iteration order while avoiding repeated context lookups.
    std::array<std::vector<const Node*>, 7 * 400> coverings_;
    std::vector<PathEntry> paths_;
    std::vector<BoardEntry> boards_;
    std::vector<WeakEntry> weak_;
    std::vector<SkyEntry> skies_;
    SkyEntry skyScratch_;
    std::size_t skyHot_ = 0;
    std::vector<unsigned> visited_, targets_;
    std::vector<const Node*> queue_, parents_;
    struct RotationEntry { const Node* from; int operation, kick; };
    std::vector<std::vector<RotationEntry>> rotationsInto_;
    std::vector<int> operations_, kicks_, distances_;
    // Within one exact board, a failed reverse filling search also proves that
    // every visited predecessor is unreachable. Reuse that proof when nearby
    // holes ask about other poses in the same inaccessible pocket.
    Rows fillBoard_{};
    std::vector<unsigned> unreachableFill_;
    unsigned fillGeneration_ = 0;
    bool fillBoardValid_ = false, fillPreviousClear_ = false;
    unsigned generation_ = 0, targetGeneration_ = 0;
    bool cacheEnabled_ = true;
    Statistics stats_;

    static Rows rows(const Map& map) {
        Rows result{}; std::copy_n(map.row, 40, result.begin()); return result;
    }
    static std::size_t hash(const Map& map, std::size_t salt) {
        std::size_t value = salt + 1469598103934665603ull;
        for (unsigned row : map.row) value = (value ^ row) * 1099511628211ull;
        return value;
    }
    static bool same(const Rows& key, const Map& map) {
        return std::equal(key.begin(), key.end(), map.row);
    }
    void prepare_fill_cache(const Map& map, bool previousClear) {
        if (cacheEnabled_ && (!fillBoardValid_ || fillPreviousClear_ != previousClear || !same(fillBoard_, map))) {
            fillBoard_ = rows(map); fillPreviousClear_ = previousClear; fillBoardValid_ = true;
            if (++fillGeneration_ == 0) { std::fill(unreachableFill_.begin(), unreachableFill_.end(), 0); ++fillGeneration_; }
        }
    }
    static void cells(Rows& out, const Node* node) {
        if (!node) return;
        for (int y = 0; y < node->height; ++y)
            if (node->row + y >= 0 && node->row + y < 40)
                out[node->row + y] |= node->data[y];
    }
    static int kind(const Landing& move) { return move.spin ? (move.mini ? 1 : 2) : 0; }
    static bool special_possible(const Map& map, const Node* node) {
        const int clear = lines(map, node);
        if (!clear) return false;
        if (clear == 4 || map.count + 4 == clear * 10) return true;
        return classify(map, node, 1).spin || classify(map, node, 5).spin;
    }
    static Landing classify(const Map& map, const Node* node, int kick) {
        Landing result(node); result.kick = kick;
        if (!kick) return result;
        Rules::SpinInput input;
        input.pose = host_pose(*node);
        input.spinChecker = node->status.t == 'T' ? Piece::SpinCheck::TCorner : Piece::SpinCheck::Immobility;
        input.lastRotation = Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(kick - 1));
        input.mapWidth = 10; input.mapHeight = 20;
        input.immobile = (!node->move_up || !node->move_up->check(map)) &&
            (!node->move_left || !node->move_left->check(map)) &&
            (!node->move_right || !node->move_right->check(map));
        const auto spin = Rules::detect_spin(input, [&](int row, int col) {
            const int y = 19 - row;
            return y >= 0 && y < 40 && col >= 0 && col < 10 && map.full(col, y);
        });
        result.spin = spin.spin; result.mini = spin.mini; return result;
    }
    static bool valid(const Witness& proof, const Map& map, const Node* start,
                      const Node* target, int wanted) {
        if (proof.start != start || proof.target.node != target || proof.kind != wanted) return false;
        for (int y = 0; y < 40; ++y)
            if ((map.row[y] & proof.empty[y]) ||
                (map.row[y] & proof.occupied[y]) != proof.occupied[y]) return false;
        return wanted == 3 || kind(classify(map, target, proof.target.kick)) == wanted;
    }
    static const Node* const* rotation(const Node* node, int operation) {
        return operation == 3 ? node->wall_kick_clockwise :
               operation == 4 ? node->wall_kick_counterclockwise : node->wall_kick_opposite;
    }
    // Retain one actual blocker for every earlier, rejected SRS kick. Merely
    // storing the successful cells could incorrectly reuse a later kick when
    // a line clear removes the blocker which made that kick legal.
    static void blockers(Witness& proof, const Map& map, const Node* parent, int operation, int kick) {
        if (operation < 3) return;
        const auto* candidates = rotation(parent, operation);
        for (int k = 0; k + 1 < kick; ++k) {
            const auto* node = candidates[k];
            for (int y = 0; y < node->height; ++y) {
                const int row = node->row + y;
                if (row < 0 || row >= 40) break;
                const unsigned occupied = node->data[y] & map.row[row];
                if (occupied) { proof.occupied[row] |= occupied & -occupied; break; }
            }
        }
    }
    SkyEntry& sky_entry(const Map& map, const Node* start) {
        auto* entry = &skyScratch_;
        if (cacheEnabled_) {
            auto& hot = skies_[skyHot_];
            if (hot.valid && hot.start == start && same(hot.rows, map)) {
                ++stats_.skyHits; return hot;
            }
            skyHot_ = hash(map, start->index) % skies_.size();
            entry = &skies_[skyHot_];
            if (entry->valid && entry->start == start && same(entry->rows, map)) {
                ++stats_.skyHits; return *entry;
            }
        }
        ++stats_.skyBuilds;
        entry->rows = rows(map); entry->start = start; entry->valid = true;
        entry->orientations.clear(); entry->columns.clear();
        Witness high; high.start = start; cells(high.empty, start);
        auto* orientation = start; int lastKick = 0;
        for (int r = 0; r < 4 && orientation; ++r) {
            entry->orientations.push_back({high, orientation, nullptr, lastKick});
            const Node* next = nullptr;
            for (int k = 0; k < m_tetris::max_wall_kick && orientation->wall_kick_clockwise[k]; ++k)
                if (orientation->wall_kick_clockwise[k]->check(map)) {
                    next = orientation->wall_kick_clockwise[k];
                    blockers(high, map, orientation, 3, k + 1); lastKick = k + 1; break;
                }
            orientation = next; cells(high.empty, orientation);
        }
        return *entry;
    }
    const std::vector<SkyRoute>& sky_column(SkyEntry& entry, const Map& map, const Node* target) {
        for (const auto& column : entry.columns)
            if (column.rotation == target->status.r && column.column == target->col) return column.routes;
        entry.columns.push_back({target->status.r, target->col, {}});
        auto& routes = entry.columns.back().routes;
        for (const auto& orientation : entry.orientations) {
            if (orientation.top->status.r != target->status.r) continue;
            auto proof = orientation.prefix; auto* top = orientation.top; int kick = orientation.kick;
            while (top->col < target->col && top->move_right && top->move_right->check(map)) {
                top = top->move_right; cells(proof.empty, top); kick = 0;
            }
            while (top->col > target->col && top->move_left && top->move_left->check(map)) {
                top = top->move_left; cells(proof.empty, top); kick = 0;
            }
            if (top->col != target->col) continue;
            auto* bottom = top;
            while (bottom->move_down && bottom->move_down->check(map)) bottom = bottom->move_down;
            routes.push_back({proof, top, bottom, kick});
        }
        return routes;
    }
    std::optional<Witness> sky_path(const Map& map, const Node* start, const Node* target, int wanted = -1,
                                   SkyEntry** reusable = nullptr) {
        // With the spawn safely above the stack, this proof only rotates in
        // the sky, translates, then drops vertically. A covered target cell
        // cannot be reached by that particular route. Reject it before walking
        // the long sky prefix; the reverse SRS search still tries tuck routes.
        // Near the stack, spawn rotations can kick, so keep the full proof.
        if (start->row >= map.roof + 4)
            for (int y = 0; y < target->height; ++y)
                for (unsigned bits = target->data[y]; bits; bits &= bits - 1)
                    if (map.top[Util::trailing_zeros32(bits)] > target->row + y) return {};
        auto* entry = reusable && *reusable ? *reusable : &sky_entry(map, start);
        if (reusable) *reusable = entry;
        for (const auto& route : sky_column(*entry, map, target)) {
            if (route.top->status.r != target->status.r || route.top->col != target->col ||
                target->row > route.top->row || target->row < route.bottom->row) continue;
            auto proof = route.prefix; auto* node = route.top; int kick = route.kick;
            while (node->row > target->row) {
                node = node->move_down; cells(proof.empty, node); kick = 0;
            }
            if (node == target && (wanted < 0 || kind(classify(map, node, kick)) == wanted)) {
                proof.target = classify(map, node, kick); proof.kind = kind(proof.target); return proof;
            }
        }
        return {};
    }
    // Search BACK from a Spin's possible final rotations, or from the target
    // itself for filling reachability (kind 3), until a checked sky path joins.
    // This
    // is the same graph as forward BFS, but avoids visiting the whole sky for
    // each reserved slot. No extra kicks or upward player inputs are invented.
    std::optional<Witness> spin_path(const Map& map, const Node* start, const Node* target, int wanted) {
        if (++generation_ == 0) { std::fill(visited_.begin(), visited_.end(), 0); ++generation_; }
        queue_.clear();
        const auto enqueue = [&](const Node* from, const Node* to, int operation, int kick, int distance) {
            if (!from || visited_[from->index] == generation_ || !from->check(map)) return;
            if (wanted == 3 && cacheEnabled_ && unreachableFill_[from->index] == fillGeneration_) {
                ++stats_.fillComponentHits; return;
            }
            visited_[from->index] = generation_; parents_[from->index] = to;
            operations_[from->index] = operation; kicks_[from->index] = kick;
            distances_[from->index] = distance; queue_.push_back(from);
        };
        const auto incoming = [&](const Node* node, int distance, bool last) {
            for (const auto& edge : rotationsInto_[node->index]) {
                if (last && wanted != 3 && kind(classify(map, target, edge.kick)) != wanted) continue;
                const auto* candidates = rotation(edge.from, edge.operation);
                bool first = true;
                for (int k = 0; k + 1 < edge.kick; ++k) if (candidates[k]->check(map)) { first = false; break; }
                if (first) enqueue(edge.from, node, edge.operation, edge.kick, distance);
            }
        };
        if (wanted == 3) enqueue(target, nullptr, 0, 0, 0);
        else {
            // A normal landing may finish with a translation, or with a
            // rotation which is not a Spin. Seed all legal final edges, then
            // use the same complete reverse SRS graph as filling/Spin queries.
            if (wanted == 0) {
                enqueue(target->move_up, target, 0, 0, 1);
                enqueue(target->move_right, target, 1, 0, 1);
                enqueue(target->move_left, target, 2, 0, 1);
            }
            incoming(target, 1, true);
        }
        SkyEntry* sky = nullptr;
        for (std::size_t i = 0; i < queue_.size(); ++i) {
            auto* node = queue_[i];
            if (auto prefix = sky_path(map, start, node, -1, &sky)) {
                auto proof = *prefix;
                auto* p = node; int finalKick = prefix->target.kick;
                for (int left = distances_[node->index]; left; --left) {
                    cells(proof.empty, p);
                    finalKick = kicks_[p->index];
                    blockers(proof, map, p, operations_[p->index], finalKick);
                    p = parents_[p->index];
                }
                cells(proof.empty, target);
                proof.target = classify(map, target, finalKick); proof.kind = wanted;
                return proof;
            }
            const int distance = distances_[node->index] + 1;
            enqueue(node->move_up, node, 0, 0, distance);
            enqueue(node->move_right, node, 1, 0, distance);
            enqueue(node->move_left, node, 2, 0, distance);
            incoming(node, distance, false);
        }
        if (wanted == 3 && cacheEnabled_)
            for (auto* node : queue_) unreachableFill_[node->index] = fillGeneration_;
        return {};
    }
    std::optional<Landing> reachable(const Map& map, const Node* start, const Node* target, int wanted) {
        if (!start || !target || !target->check(map) ||
            (target->move_down && target->move_down->check(map))) return {};
        if (wanted == 3 && cacheEnabled_ && unreachableFill_[target->index] == fillGeneration_) {
            ++stats_.fillComponentHits; return {};
        }
        const auto index = (target->index * 4 + wanted) % paths_.size();
        auto& entry = paths_[index];
        if (cacheEnabled_ && valid(entry.proof, map, start, target, wanted)) {
            ++stats_.pathHits; return entry.proof.target;
        }
        if (cacheEnabled_ && entry.failedStart == start && entry.failedTarget == target &&
            entry.failedKind == wanted && same(entry.failedBoard, map)) {
            ++stats_.pathHits; return {};
        }
        if (wanted > 0 && wanted != 3) {
            bool possible = false;
            for (const auto& edge : rotationsInto_[target->index]) {
                if (kind(classify(map, target, edge.kick)) != wanted || !edge.from->check(map)) continue;
                const auto* candidates = rotation(edge.from, edge.operation);
                bool first = true;
                for (int k = 0; k + 1 < edge.kick; ++k) if (candidates[k]->check(map)) { first = false; break; }
                if (first) { possible = true; break; }
            }
            if (!possible) return {};
        }
        ++stats_.pathMisses; ++stats_.enumerations;
        if (wanted == 0 || wanted == 3) {
            if (auto proof = sky_path(map, start, target, wanted == 3 ? -1 : wanted)) {
                proof->kind = wanted;
                if (cacheEnabled_) entry.proof = *proof;
                return proof->target;
            }
        }
        ++stats_.bfs;
        if (auto proof = spin_path(map, start, target, wanted)) {
            if (cacheEnabled_) entry.proof = *proof;
            return proof->target;
        }
        if (cacheEnabled_) {
            entry.failedBoard = rows(map); entry.failedStart = start;
            entry.failedTarget = target; entry.failedKind = wanted;
        }
        return {};
    }
    const Node* aligned(char family, const Node* anchor, int x, int y, int ax, int ay) const {
        const int nx = int(anchor->status.x) + x - ax;
        const int ny = int(anchor->status.y) + y - anchor->row - ay;
        if (nx < -4 || nx > 10 || ny < 0 || ny >= 44) return nullptr;
        const auto* node = context_->get(family, nx, ny, anchor->status.r);
        return node && node->row >= 0 && node->row + node->height <= 40 ? node : nullptr;
    }
    template<class F> void covering(char family, int x, int y, F&& visit) const {
        if (x < 0 || x >= 10 || y < 0 || y >= 40) return;
        for (const auto* node : coverings_[piece_index(family) * 400 + y * 10 + x]) visit(node);
    }
public:
    void init(const m_tetris::TetrisContext* context) {
        context_ = context;
        paths_.assign(4096, {}); boards_.assign(2048, {}); weak_.assign(512, {});
        skies_.assign(256, {}); skyScratch_ = {}; skyHot_ = 0;
        visited_.assign(context->node_max(), 0); targets_.assign(context->node_max(), 0);
        unreachableFill_.assign(context->node_max(), 0); fillGeneration_ = 0; fillBoardValid_ = false;
        parents_.resize(context->node_max()); operations_.resize(context->node_max()); kicks_.resize(context->node_max()); distances_.resize(context->node_max());
        generation_ = targetGeneration_ = 0; stats_ = {};
        rotationsInto_.assign(context->node_max(), {});
        std::vector<bool> indexed(context->node_max(), false);
        for (char family : std::string_view("IJLOSTZ")) for (int r = 0; r < 4; ++r)
            for (int y = 0; y < 44; ++y) for (int x = -4; x <= 10; ++x) {
                const auto* node = context->get(family, x, y, r);
                if (!node || indexed[node->index]) continue;
                indexed[node->index] = true;
                for (int op = 3; op <= 5; ++op) {
                    const auto* candidates = rotation(node, op);
                    for (int k = 0; k < m_tetris::max_wall_kick && candidates[k]; ++k)
                        rotationsInto_[candidates[k]->index].push_back({node, op, k + 1});
                }
            }
        for (int t = 0; t < 7; ++t) for (int r = 0; r < 4; ++r)
            anchors_[t][r] = context->get("IJLOSTZ"[t], 3, 22, r);
        for (int t = 0; t < 7; ++t) for (int y = 0; y < 40; ++y) for (int x = 0; x < 10; ++x) {
            auto& placements = coverings_[t * 400 + y * 10 + x]; placements.clear();
            for (auto* anchor : anchors_[t]) if (anchor)
                for (int ay = 0; ay < anchor->height; ++ay)
                    for (unsigned bits = anchor->data[ay]; bits; bits &= bits - 1)
                        if (auto* node = aligned("IJLOSTZ"[t], anchor, x, y, Util::trailing_zeros32(bits), ay))
                            placements.push_back(node);
        }
    }
    void set_cache_enabled(bool enabled) { cacheEnabled_ = enabled; }
    void reset_statistics() { stats_ = {}; } // Does NOT discard reusable geometry.
    const Statistics& statistics() const { return stats_; }
    // All seven families, irrespective of NEXT. A family counts only when a
    // complete piece can legally reach AND lock over this cell. Final fitting
    // geometry or air connectivity alone is insufficient. Translation after
    // rotation is allowed; filling need not score a Spin under attack rules.
    unsigned filling_types(const Map& map, int x, int y, bool previousClear) {
        prepare_fill_cache(map, previousClear);
        unsigned result = 0;
        for (int t = 0; t < 7; ++t) {
            const char family = "IJLOSTZ"[t];
            const auto* start = spawn_node(context_, map, family, previousClear);
            if (!start) continue;
            covering(family, x, y, [&](const Node* target) {
                if (!(result & (1u << t)) && reachable(map, start, target, 3)) result |= 1u << t;
            });
        }
        return result;
    }
    static int lines(const Map& map, const Node* node) {
        int result = 0;
        for (int y = 0; y < node->height; ++y) result += (map.row[node->row + y] | node->data[y]) == 1023u;
        return result;
    }
    std::vector<const Node*> quads(const Map& map) const {
        std::vector<const Node*> result;
        for (int y = 0; y + 3 < map.roof; ++y) {
            const unsigned hole = 1023u ^ map.row[y];
            if (!hole || (hole & (hole - 1)) || map.row[y + 1] != map.row[y] ||
                map.row[y + 2] != map.row[y] || map.row[y + 3] != map.row[y]) continue;
            const int x = Util::trailing_zeros32(hole);
            if (map.top[x] != y) continue;
            covering('I', x, y, [&](const Node* node) {
                if (node->height == 4 && node->row == y && node->col == x &&
                    std::none_of(result.begin(), result.end(), [&](const Node* old) { return old->row == y && old->col == x; })) result.push_back(node);
            });
        }
        return result;
    }
    std::optional<Landing> landing(const Map& map, const Node* target, bool previousClear,
                                   int requiredKind = -1) {
        if (!target) return {};
        auto* start = spawn_node(context_, map, target->status.t, previousClear);
        if (!start || !target->check(map) || (target->move_down && target->move_down->check(map))) return {};
        if (requiredKind == 3) prepare_fill_cache(map, previousClear);
        if (requiredKind >= 0) return reachable(map, start, target, requiredKind);
        for (int wanted : {2, 1}) {
            const bool possible = kind(classify(map, target, 1)) == wanted || kind(classify(map, target, 5)) == wanted;
            if (possible) if (auto result = reachable(map, start, target, wanted)) return result;
        }
        return reachable(map, start, target, 0);
    }
    bool preserves(const Map& map, const Landing& key, bool previousClear) {
        return lines(map, key.node) > 0 && bool(landing(map, key.node, previousClear, kind(key)));
    }
    std::vector<Landing> ready(const Map& map, char family, bool previousClear) {
        auto* start = spawn_node(context_, map, family, previousClear);
        if (!start) return {};
        const auto slot = hash(map, start->index) % boards_.size();
        auto& entry = boards_[slot];
        if (cacheEnabled_ && entry.valid && entry.start == start && same(entry.rows, map)) {
            ++stats_.boardHits; return entry.keys;
        }
        if (++targetGeneration_ == 0) { std::fill(targets_.begin(), targets_.end(), 0); ++targetGeneration_; }
        std::vector<Landing> result;
        for (int y = 0; y < map.roof; ++y) {
            const unsigned holes = 1023u ^ map.row[y];
            if (!holes || Util::popcount32(holes) > 4) continue;
            covering(family, Util::trailing_zeros32(holes), y, [&](const Node* node) {
                if (targets_[node->index] == targetGeneration_) return;
                targets_[node->index] = targetGeneration_;
                if (!node->check(map) || (node->move_down && node->move_down->check(map)) || !special_possible(map, node)) return;
                ++stats_.targets;
                if (auto move = landing(map, node, previousClear)) result.push_back(*move);
            });
        }
        if (cacheEnabled_) { entry.rows = rows(map); entry.start = start; entry.keys = result; entry.valid = true; }
        return result;
    }
    // Bounded near-completion detector: a dense row is missing at most four
    // cells; the key fills some and ONE real preparation fills all remaining
    // cells. Validate both complete moves, including any prep line shift.
    std::vector<Preparation> weak(const Map& map, char prepare, char family, bool previousClear, bool quadOnly = false) {
        const auto slot = hash(map, unsigned(prepare) * 512 + unsigned(family) * 4 + previousClear * 2 + quadOnly) % weak_.size();
        auto& entry = weak_[slot];
        if (cacheEnabled_ && entry.valid && entry.prepare == prepare && entry.key == family &&
            entry.previousClear == previousClear && entry.quadOnly == quadOnly && same(entry.rows, map)) { ++stats_.weakHits; return entry.plans; }
        std::vector<Preparation> result;
        std::vector<std::pair<std::size_t, std::size_t>> seen;
        const auto consider = [&](const Node* key, const Node* prep) {
            if (!prep->check(map) || (prep->move_down && prep->move_down->check(map))) return;
            for (int r = std::max(key->row, prep->row); r < std::min(key->row + key->height, prep->row + prep->height); ++r)
                if (key->data[r - key->row] & prep->data[r - prep->row]) return;
            const auto pair = std::make_pair(prep->index, key->index);
            if (std::find(seen.begin(), seen.end(), pair) != seen.end()) return;
            seen.push_back(pair);
            Map after = map; const int cleared = prep->attach(context_, after);
            int shift = 0;
            for (int r = prep->row; r < std::min(key->row, prep->row + prep->height); ++r)
                shift += (map.row[r] | prep->data[r - prep->row]) == 1023u;
            const auto* movedKey = context_->get(family, key->status.x, key->status.y - shift, key->status.r);
            if (!movedKey || !movedKey->check(after) || !special_possible(after, movedKey)) return;
            auto finish = landing(after, movedKey, cleared > 0); if (!finish) return;
            // A preparation must add a clear, open the entry, or upgrade a
            // normal/Mini placement to the advertised Spin category.
            if (lines(map, key) >= lines(after, movedKey)) {
                const auto old = landing(map, key, previousClear);
                if (old && kind(*old) >= kind(*finish)) return;
            }
            auto first = landing(map, prep, previousClear); if (!first) return;
            result.push_back({*first, *finish, cleared, key});
        };
        if (family == 'I') for (int y = 0; y + 2 < map.roof; ++y) {
            const unsigned hole = 1023u ^ map.row[y];
            if (!hole || (hole & (hole - 1)) || map.row[y + 1] != map.row[y] || map.row[y + 2] != map.row[y]) continue;
            Rows needed{}; int count = 0;
            for (int r = y + 3; r < map.roof; ++r) if (map.row[r] & hole) {
                needed[r] = 1023u ^ map.row[r]; count += Util::popcount32(needed[r]);
            }
            int fourth = y + 3;
            while (fourth < map.roof && (map.row[fourth] & hole)) ++fourth;
            if (fourth >= 40) continue;
            needed[fourth] = (1023u ^ map.row[fourth]) & ~hole;
            count += Util::popcount32(needed[fourth]);
            if (!count || count > 4) continue;
            int row = 0; while (!needed[row]) ++row;
            covering(prepare, Util::trailing_zeros32(needed[row]), row, [&](const Node* prep) {
                if (!prep->check(map) || (prep->move_down && prep->move_down->check(map))) return;
                for (int r = 0; r < 40; ++r) if (needed[r] &&
                    (r < prep->row || r >= prep->row + prep->height || (prep->data[r - prep->row] & needed[r]) != needed[r])) return;
                Map after = map; const int clear = prep->attach(context_, after);
                for (const auto* key : quads(after)) {
                    const auto finish = landing(after, key, clear > 0), first = landing(map, prep, previousClear);
                    if (finish && first) result.push_back({*first, *finish, clear, nullptr});
                }
            });
        }
        if (!quadOnly) for (int y = 0; y < map.roof; ++y) {
            const unsigned holes = 1023u ^ map.row[y];
            if (Util::popcount32(holes) < 2 || Util::popcount32(holes) > 4) continue;
            for (unsigned bits = holes; bits; bits &= bits - 1) covering(family, Util::trailing_zeros32(bits), y, [&](const Node* key) {
                if (!key->check(map)) return;
                const unsigned needed = holes & ~key->data[y - key->row];
                if (!needed || needed == holes) return;
                covering(prepare, Util::trailing_zeros32(needed), y, [&](const Node* prep) {
                    if ((prep->data[y - prep->row] & needed) == needed) consider(key, prep);
                });
            });
        }
        // Finishing the roof/corners can be the indispensable preparation,
        // even if the row could already be cleared as an ordinary placement.
        std::vector<std::size_t> upgraded;
        if (!quadOnly) for (int y = 0; y < map.roof; ++y) {
            const unsigned holes = 1023u ^ map.row[y];
            if (!holes || Util::popcount32(holes) > 4) continue;
            covering(family, Util::trailing_zeros32(holes), y, [&](const Node* target) {
                if (!target->check(map) || !lines(map, target) ||
                    std::find(upgraded.begin(), upgraded.end(), target->index) != upgraded.end()) return;
                upgraded.push_back(target->index);
            Rows adjacent{};
            for (auto* node : {target->move_up, target->move_left, target->move_right}) cells(adjacent, node);
            if (family == 'T') {
                const auto pose = host_pose(*target);
                for (int dy : {-1, 1}) for (int dx : {-1, 1}) {
                    const int y = 19 - pose.position.row + dy, x = pose.position.col + dx;
                    if (y >= 0 && y < 40 && x >= 0 && x < 10) adjacent[y] |= 1u << x;
                }
            }
            Rows footprint{}; cells(footprint, target);
            for (int y = 0; y < std::min(40, map.roof + 3); ++y)
                for (unsigned bits = adjacent[y] & ~footprint[y] & ~map.row[y]; bits; bits &= bits - 1)
                    covering(prepare, Util::trailing_zeros32(bits), y, [&](const Node* prep) { consider(target, prep); });
            });
        }
        if (cacheEnabled_) {
            entry.rows = rows(map); entry.prepare = prepare; entry.key = family;
            entry.previousClear = previousClear; entry.quadOnly = quadOnly; entry.plans = result; entry.valid = true;
        }
        return result;
    }
    // Cheap, constructive parking routes. Every rotation/translation/drop is
    // checked from the real spawn. A failed parking proof loses the estimate;
    // it never invents a way to wait for the reserved piece.
    std::vector<Landing> parking(const Map& map, char family, bool previousClear) const {
        std::vector<Landing> result;
        auto* orientation = spawn_node(context_, map, family, previousClear);
        for (int r = 0; r < 4 && orientation; ++r) {
            auto* left = orientation;
            while (left->move_left && left->move_left->check(map)) left = left->move_left;
            for (auto* node = left; node && node->check(map); node = node->move_right) {
                auto* dropped = node->drop(map);
                if (!lines(map, dropped) && std::none_of(result.begin(), result.end(), [&](const Landing& p) {
                    return p->index_filtered == dropped->index_filtered;
                })) result.emplace_back(dropped);
            }
            auto* next = static_cast<const Node*>(nullptr);
            for (int k = 0; k < m_tetris::max_wall_kick && orientation->wall_kick_clockwise[k]; ++k)
                if (orientation->wall_kick_clockwise[k]->check(map)) { next = orientation->wall_kick_clockwise[k]; break; }
            orientation = next;
        }
        return result;
    }
};
}
#endif
