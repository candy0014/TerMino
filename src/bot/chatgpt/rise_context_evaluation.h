#ifndef TETR_BOT_CHATGPT_RISE_CONTEXT_EVALUATION_H
#define TETR_BOT_CHATGPT_RISE_CONTEXT_EVALUATION_H

#include "evaluation.h"
#include <array>
#include <cstdint>

namespace Bot_chatgpt {

// P3-B: validate the entry to a virtual post-rise continuation. Unknown garbage
// is EMPTY in this permissive geometry; this is not a predicted hole pattern.
// Keep every potentially first-successful kick and possible Clutch start, so it
// must not reject a route merely because removing unknown cells changed the
// first successful kick. No new moves or Spin rewards enter the native search.
class RiseContextEvaluation : public Evaluation {
public:
    struct Statistics { std::size_t checks = 0, floods = 0, rejected = 0, visited = 0; };

    void init(const m_tetris::TetrisContext* context) {
        Evaluation::init(context);
        context_ = context;
        marks_.assign(context->node_max(), 0);
        invalidate();
    }

    void set_garbage_forecast(const std::vector<VisiblePacket>& packets, double step) {
        Evaluation::set_garbage_forecast(packets, step);
        invalidate(); stats_ = {};
    }

    const Statistics& statistics() const { return stats_; }

    bool continuation_possible(const Landing& landing, const m_tetris::TetrisMap& before,
                               int risen, bool previousCleared) const {
        if (risen <= 0) return true;
        const auto* normal = context_->generate(landing->status.t);
        // Existing terrain remains wholly below spawn: keep the original fast
        // path. This shortcut only declines to filter; it certifies no route.
        if (before.roof + risen < normal->low) return true;
        ++stats_.checks;
        if (parent_ != &before || risen_ != risen || clutch_ != previousCleared ||
            !std::equal(rows_.begin(), rows_.end(), before.row)) {
            parent_ = &before; risen_ = risen; clutch_ = previousCleared;
            std::copy_n(before.row, 40, rows_.begin());
            known_ = m_tetris::TetrisMap(10, 40);
            // Only known occupied cells are raised. Lost cells above the host
            // storage are not invented as obstacles below it.
            for (int y = 0; y < 40; ++y) {
                known_.row[y] = y >= risen ? before.row[y - risen] : 0;
                for (int x = 0; x < 10; ++x) if (known_.row[y] & (1u << x)) {
                    known_.top[x] = y + 1; known_.roof = y + 1; ++known_.count;
                }
            }
            built_.fill(false);
            if (++generation_ == 0) {
                std::fill(marks_.begin(), marks_.end(), 0); ++generation_;
            }
        }
        const int family = piece_index(landing->status.t);
        if (!built_[family]) {
            built_[family] = true; ++stats_.floods; queue_.clear();
            const auto fits = [&](const m_tetris::TetrisNode* node) {
                return node && node->row >= 0 && node->row + node->height <= 40 && node->check(known_);
            };
            const auto enqueue = [&](const m_tetris::TetrisNode* node) {
                if (fits(node) && marks_[node->index] != generation_) {
                    marks_[node->index] = generation_; queue_.push_back(node); ++stats_.visited;
                }
            };
            for (auto* start = normal; start; start = start->move_up) {
                if (fits(start)) {
                    enqueue(start);
                    // Entirely above unknown garbage: this spawn is guaranteed
                    // free, so real Clutch cannot skip it for a higher start.
                    if (start->row >= risen) break;
                }
                if (!previousCleared) break;
            }
            for (std::size_t i = 0; i < queue_.size(); ++i) {
                const auto* node = queue_[i];
                enqueue(node->move_down); enqueue(node->move_left); enqueue(node->move_right);
                for (const auto* kicks : {node->wall_kick_clockwise,
                                          node->wall_kick_counterclockwise, node->wall_kick_opposite})
                    for (int k = 0; k < m_tetris::max_wall_kick && kicks[k]; ++k) {
                        if (!fits(kicks[k])) continue;
                        enqueue(kicks[k]);
                        // A kick touching unknown cells may fail on the actual
                        // board. Otherwise it is certainly the first success;
                        // allowing later kicks would invent climbing in air.
                        if (kicks[k]->row >= risen) break;
                    }
            }
        }
        auto pose = host_pose(*landing.node);
        pose.position.row -= risen;
        const auto* translated = native_pose(context_, landing->status.t, pose);
        return translated && marks_[translated->index] == generation_;
    }

    Status get(Landing& landing, const Result& evaluated, std::size_t depth,
               const Status& previous, const m_tetris::TetrisContext::Env& env) const {
        if (previous.dead) return previous;
        if (previous.risen > 0 && evaluated.before &&
            !continuation_possible(landing, *evaluated.before, previous.risen, previous.previousCleared)) {
            ++stats_.rejected;
            Status result = previous;
            // Unusable continuation, not a new host death or a claim that every
            // alternative dies. Freeze before the impossible placement: it may
            // not earn attack, clear rows, charge B2B or advance visible time.
            result.dead = true;
            result.value -= 1e9;
            return result;
        }
        return Evaluation::get(landing, evaluated, depth, previous, env);
    }

private:
    void invalidate() { parent_ = nullptr; }
    const m_tetris::TetrisContext* context_ = nullptr;
    mutable const m_tetris::TetrisMap* parent_ = nullptr;
    mutable int risen_ = 0;
    mutable bool clutch_ = false;
    mutable std::array<std::uint32_t, 40> rows_{};
    mutable m_tetris::TetrisMap known_{10, 40};
    mutable std::array<bool, 7> built_{};
    mutable std::vector<std::uint32_t> marks_;
    mutable std::uint32_t generation_ = 0;
    mutable std::vector<const m_tetris::TetrisNode*> queue_;
    mutable Statistics stats_;
};

} // namespace Bot_chatgpt
#endif
