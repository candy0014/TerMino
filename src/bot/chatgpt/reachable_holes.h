#include "tetr/util/bit_ops.h"
#ifndef TETR_BOT_CHATGPT_REACHABLE_HOLES_H
#define TETR_BOT_CHATGPT_REACHABLE_HOLES_H

#include "key_piece_geometry.h"
#include <cassert>

namespace Bot_chatgpt {

// Geometry is persistent; supply-dependent scores are never cached. This
// replaces the THREE original hole terms, retaining other terrain features.
class ReachableHoles {
public:
    using Map = KeyPieceGeometry::Map;
    struct Cell {
        int x = 0, y = 0, covers = 0;
        unsigned families = 0;
        bool connected = false;
    };
    struct Geometry {
        std::vector<Cell> cells;
        double legacyPenalty = 0;
    };
    struct Score {
        double legacyPenalty = 0, penalty = 0;
        int fillable = 0, disconnected = 0, connectedBlocked = 0;
        double adjustment() const { return legacyPenalty - penalty; }
    };
    struct Statistics { std::uint64_t calls = 0, boardHits = 0, analyses = 0, fillingQueries = 0; };
    static constexpr double blockedBase = 400, coverWeight = 80;
    static constexpr double fillableBase = 240, diversityWeight = 0.25;
private:
    struct Entry {
        KeyPieceGeometry::Rows rows{};
        Geometry geometry;
        bool valid = false, previousClear = false;
    };
    std::vector<Entry> cache_;
    Geometry scratch_;
    bool cacheEnabled_ = true;
    Statistics stats_;

    static Geometry describe(const Map& map) {
        Geometry result;
        unsigned cover = 0;
        std::array<int, 10> solidAbove{}, depths{};
        double rate = 32;
        for (int y = map.roof - 1; y >= 0; --y) {
            cover |= map.row[y];
            const unsigned holes = cover & ~map.row[y];
            if (holes) {
                result.legacyPenalty += 380;
                int clearWidth = 0;
                for (int hy = y + 1; hy < map.roof; ++hy) {
                    const auto intersection = holes & map.row[hy];
                    if (!intersection) break;
                    clearWidth += (hy + 1) * Util::popcount32(intersection);
                }
                result.legacyPenalty += rate * clearWidth;
                rate *= 0.25;
            }
            for (int x = 0; x < 10; ++x) {
                if (holes & (1u << x)) {
                    result.legacyPenalty += 40 * ++depths[x];
                    result.cells.push_back({x, y, solidAbove[x], 0, false});
                } else depths[x] = 0;
                solidAbove[x] += (map.row[y] >> x) & 1;
            }
        }
        return result;
    }
    static KeyPieceGeometry::Rows top_connected(const Map& map) {
        KeyPieceGeometry::Rows air{};
        std::array<int, 400> queue{};
        int head = 0, tail = 0;
        const int top = std::min(39, map.roof);
        const auto add = [&](int x, int y) {
            if (x < 0 || x >= 10 || y < 0 || y > top ||
                ((map.row[y] | air[y]) & (1u << x))) return;
            air[y] |= 1u << x;
            queue[tail++] = y * 10 + x;
        };
        for (int x = 0; x < 10; ++x) add(x, top);
        while (head < tail) {
            const int cell = queue[head++], x = cell % 10, y = cell / 10;
            add(x - 1, y); add(x + 1, y); add(x, y - 1); add(x, y + 1);
        }
        return air;
    }
public:
    void init() { cache_.assign(2048, {}); scratch_ = {}; stats_ = {}; }
    void set_cache_enabled(bool enabled) { cacheEnabled_ = enabled; }
    void reset_statistics() { stats_ = {}; }
    const Statistics& statistics() const { return stats_; }

    const Geometry& analyze(const Map& map, bool previousClear, KeyPieceGeometry& paths) {
        assert(map.width == 10 && map.height == 40 && !cache_.empty());
        ++stats_.calls;
        std::size_t hash = 1469598103934665603ull + previousClear;
        for (auto row : map.row) hash = (hash ^ row) * 1099511628211ull;
        auto& entry = cache_[hash % cache_.size()];
        if (cacheEnabled_ && entry.valid && entry.previousClear == previousClear &&
            std::equal(entry.rows.begin(), entry.rows.end(), map.row)) {
            ++stats_.boardHits;
            return entry.geometry;
        }
        ++stats_.analyses;
        auto result = describe(map);
        if (!result.cells.empty()) {
            const auto air = top_connected(map);
            for (auto& cell : result.cells) {
                cell.connected = (air[cell.y] >> cell.x) & 1;
                if (!cell.connected) continue;
                ++stats_.fillingQueries;
                cell.families = paths.filling_types(map, cell.x, cell.y, previousClear);
            }
        }
        if (!cacheEnabled_) { scratch_ = std::move(result); return scratch_; }
        std::copy_n(map.row, 40, entry.rows.begin()); entry.previousClear = previousClear;
        entry.geometry = std::move(result); entry.valid = true;
        return entry.geometry;
    }

    // Distance counts earlier LOCKS, not array indices: an empty HOLD can
    // skip one visible piece. A held piece requires a known current piece.
    // Unseen families have no arrival credit; no hidden bag order is invented.
    static std::array<int, 7> distances(std::string_view next, char hold, bool canHold) {
        std::array<int, 7> result; result.fill(-1);
        if (next.empty()) return result;
        for (std::size_t i = 0; i < next.size(); ++i) {
            const int t = piece_index(next[i]);
            if (t >= 0 && result[t] < 0)
                result[t] = std::max(0, int(i) - int(canHold && hold == ' ' && i > 0));
        }
        const int held = piece_index(hold);
        if (canHold && held >= 0) result[held] = 0;
        return result;
    }
    static double cell_penalty(const Cell& cell, const std::array<int, 7>& distance) {
        if (!cell.connected || !cell.families) return blockedBase + coverWeight * cell.covers;
        double supply = 0;
        for (int t = 0; t < 7; ++t)
            if ((cell.families & (1u << t)) && distance[t] >= 0) supply += 1.0 / (1 + distance[t]);
        return fillableBase / (1 + diversityWeight * Util::popcount32(cell.families) + supply);
    }
    Score evaluate(const Map& map, bool previousClear, std::string_view next, char hold,
                   bool canHold, KeyPieceGeometry& paths, int risen = 0) {
        // Unknown risen garbage is a solid substrate for path checks, as in
        // spawn_node. Shift known cells; never invent useful garbage holes.
        Map shifted(10, 40);
        if (risen > 0 && map.roof + risen <= 40) {
            for (int y = 0; y < 40; ++y) {
                shifted.row[y] = y < risen ? 1023u : map.row[y - risen];
                for (int x = 0; x < 10; ++x) if (shifted.row[y] & (1u << x)) {
                    shifted.top[x] = y + 1; shifted.roof = y + 1; ++shifted.count;
                }
            }
        }
        const bool overflow = risen > 0 && map.roof + risen > 40;
        const auto& data = analyze(risen > 0 && !overflow ? shifted : map, previousClear, paths);
        Score result;
        result.legacyPenalty = risen > 0 ? describe(map).legacyPenalty : data.legacyPenalty;
        const auto arrival = distances(next, hold, canHold);
        for (auto cell : data.cells) {
            if (overflow) cell.families = 0;
            result.penalty += cell_penalty(cell, arrival);
            if (!cell.connected) ++result.disconnected;
            else if (!cell.families) ++result.connectedBlocked;
            else ++result.fillable;
        }
        return result;
    }
};
}
#endif
