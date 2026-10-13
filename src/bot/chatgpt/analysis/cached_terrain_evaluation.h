#ifndef TETR_BOT_CHATGPT_ANALYSIS_CACHED_TERRAIN_EVALUATION_H
#define TETR_BOT_CHATGPT_ANALYSIS_CACHED_TERRAIN_EVALUATION_H
// Offline feasibility experiment. Cache only Botris's map-only scalar terrain.
// No playing-policy connection. Before/clear/map pointers are never cached.
#include "../evaluation.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace Bot_chatgpt::Analysis {
template<bool Verify = false, bool ForceHashCollision = false>
class CachedTerrainEvaluation : public Evaluation {
    struct Entry {
        std::array<std::uint32_t, 40> rows{};
        std::uint64_t hash = 0;
        double value = 0;
        std::uint32_t epoch = 0;
        int roof = 0, count = 0;
    };
    static constexpr std::size_t capacity = 16384;
    static_assert(capacity * sizeof(Entry) < (16ull << 20));
    mutable std::vector<Entry> entries_;
    std::uint32_t epoch_ = 0;
    ai_zzz::Botris terrain_;
    const m_tetris::TetrisContext* context_ = nullptr;
public:
    struct Counters {
        std::uint64_t calls = 0, hits = 0, misses = 0, replacements = 0,
            hashCollisions = 0, empty = 0, fallback = 0, verified = 0;
    };
    mutable Counters counters;
    static constexpr std::size_t cache_bytes() { return capacity * sizeof(Entry); }
    void reset_root() {
        counters = {};
        if (++epoch_ == 0) { for (auto& e : entries_) e.epoch = 0; ++epoch_; }
    }
    void init(const m_tetris::TetrisContext* context) {
        Evaluation::init(context);
        context_ = context; terrain_.init(context, nullptr);
        entries_.resize(capacity);
        reset_root(); // Context change never inherits an old context's entry.
    }
    void set_garbage_forecast(const std::vector<VisiblePacket>& packets, double step) {
        Evaluation::set_garbage_forecast(packets, step);
        reset_root(); // Called once by the caller for each actual decision root.
    }
    Result eval(const Landing& landing, const m_tetris::TetrisMap& map,
                const m_tetris::TetrisMap& before, std::size_t clear) const {
        ++counters.calls;
        if (map.width != 10 || map.height != 40) {
            ++counters.fallback; return Evaluation::eval(landing, map, before, clear);
        }
        Result out;
        out.before = &before;
        if (map.roof == 0) {
            ++counters.empty;
            out.shape = {-16000.0, static_cast<int>(clear), 0, &map};
        } else {
            std::uint64_t hash = 1469598103934665603ull;
            for (int y = 0; y < 40; ++y) { hash ^= map.row[y]; hash *= 1099511628211ull; }
            hash ^= hash >> 33; hash *= 0xff51afd7ed558ccdull; hash ^= hash >> 33;
            if constexpr (ForceHashCollision) hash = 0;
            auto& entry = entries_[hash & (capacity - 1)];
            const bool sameHash = entry.epoch == epoch_ && entry.hash == hash;
            const bool hit = sameHash && entry.roof == map.roof && entry.count == map.count &&
                std::memcmp(entry.rows.data(), map.row, sizeof(entry.rows)) == 0;
            double value;
            if (hit) { ++counters.hits; value = entry.value; }
            else {
                ++counters.misses; counters.replacements += entry.epoch == epoch_;
                counters.hashCollisions += sameHash;
                const auto fresh = terrain_.eval(ai_zzz::Botris::TetrisNodeEx(landing.node), map, before, clear);
                value = fresh.value;
                // Botris::eval's HoleCount is zero-initialized and never changed;
                // count is map.count. Guard this audited source assumption.
                if (fresh.count != map.count) throw std::runtime_error("Botris count dependency changed");
                std::memcpy(entry.rows.data(), map.row, sizeof(entry.rows));
                entry.hash = hash; entry.value = value; entry.epoch = epoch_;
                entry.roof = map.roof; entry.count = map.count;
            }
            out.shape = {value, static_cast<int>(clear), map.count, &map};
        }
        // These fields depend on this landing/before board and MUST be fresh.
        for (int y = 0; y < landing->height; ++y) {
            const int row = landing->row + y;
            if ((before.row[row] | landing->data[y]) == context_->full())
                out.clearedRows |= std::uint64_t{1} << row;
        }
        if constexpr (Verify) {
            const auto original = Evaluation::eval(landing, map, before, clear);
            ++counters.verified;
            if (out.shape.value != original.shape.value || out.shape.clear != original.shape.clear ||
                out.shape.count != original.shape.count || out.shape.map != original.shape.map ||
                out.before != original.before || out.clearedRows != original.clearedRows)
                throw std::runtime_error("cached evaluation differs from original");
        }
        return out;
    }
};
}
#endif
