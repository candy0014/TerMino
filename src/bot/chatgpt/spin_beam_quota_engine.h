#ifndef TETR_BOT_CHATGPT_SPIN_BEAM_QUOTA_ENGINE_H
#define TETR_BOT_CHATGPT_SPIN_BEAM_QUOTA_ENGINE_H
// P2-B experiment. P2-A stays frozen in spin_quota_engine.h. Private scheduler over upstream public tree types: neither
// the opponent's engine nor the shared upstream header is modified.
#include "evaluation.h"
#include "rule_tetr_srs_plus.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>

namespace Bot_chatgpt {
class SpinBeamQuotaEngine {
    // Tests drive the same scheduler with fixed work counts; no test clock or
    // work-budget branch is added to the production timed entry points.
    friend struct SpinBeamQuotaTestAccess;
    using Native = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, Evaluation, Search>;
    using Node = Native::TreeNode;
    using Builder = Native::ContextBuilder;
    using Clock = std::chrono::steady_clock;
    std::deque<Node> nodes_;
    std::shared_ptr<m_tetris::TetrisContext> context_;
    Builder::LocalContext<Node> local_{&nodes_};
    Node* root_ = nullptr;
    Evaluation ai_;
    Search search_;
    Evaluation::Status status_;
    std::uint64_t memoryLimit_ = 64ull << 20;
    int quotaPercent_ = 20;
    std::size_t slotCursor_ = 0;

    void bind() {
        local_.engine = context_.get(); local_.ai = &ai_; local_.search = &search_;
        Builder::init_ai(ai_, &local_, context_.get());
        Builder::init_search(search_, &local_, context_.get());
    }

    struct Pending {
        Node* node;
        bool hasSpin;
        unsigned setupSteps;
    };
    static constexpr std::size_t beamWidth = 4;
    static constexpr unsigned maxSetupSteps = 2;

    template<bool Hold>
    void expand_opportunity(Node* node, std::size_t slot, Clock::time_point deadline) {
        std::vector<Pending> layer{{node, false, 0}};
        while (!layer.empty()) {
            stats_.peakBeam = std::max(stats_.peakBeam, layer.size());
            std::vector<Pending> next;
            for (std::size_t i = 0; i < layer.size(); ++i) {
                // Only unexpanded work is held locally. On timeout return every
                // pending sibling to its original depth; no duplicate entries.
                if (Clock::now() >= deadline) {
                    for (; i < layer.size(); ++i) local_.wait[slot].push(layer[i].node);
                    for (const auto& item : next) local_.wait[slot - 1].push(item.node);
                    return;
                }
                const auto item = layer[i];
                local_.sort[slot].push(item.node);
                auto* children = item.node->template build_children<Hold>(&local_);
                ++stats_.extraExpansions;
                if (item.hasSpin) ++stats_.continuationExpansions;
                else if (item.setupSteps) ++stats_.preparatoryExpansions;
                for (auto* child = children; child; child = child->children_next) {
                    const auto& state = child->status.get_raw();
                    const bool knownPlayable = item.node->node != ' ' && !state.dead && state.risen == 0;
                    const bool spinExit = knownPlayable && child->identity.spin && child->result.shape.clear > 0;
                    if (spinExit) ++stats_.verifiedSpinExits;
                    const bool hasSpin = item.hasSpin || spinExit;
                    const unsigned setupSteps = item.setupSteps + !hasSpin;
                    if (slot == 1) {
                        // Original model leaves may include the known-HOLD tail;
                        // this counter does NOT certify full-path host legality.
                        ++stats_.modelLeaves;
                        local_.wait[0].push(child);
                    } else if (knownPlayable && (hasSpin || setupSteps <= maxSetupSteps)) {
                        next.push_back({child, hasSpin, setupSteps});
                    } else {
                        local_.wait[slot - 1].push(child);
                    }
                }
            }
            const auto keep = std::min(beamWidth, next.size());
            auto better = [](const Pending& a, const Pending& b) {
                return b.node->status.get() < a.node->status.get();
            };
            std::partial_sort(next.begin(), next.begin() + keep, next.end(), better);
            // One reserved certified-Spin lane, not a score bonus. Up to three
            // ordinary/setup/release competitors survive alongside it.
            const bool retainedSpin = std::any_of(next.begin(), next.begin() + keep,
                [](const Pending& item) { return item.hasSpin; });
            if (keep && !retainedSpin) {
                auto bestSpin = next.end();
                for (auto it = next.begin() + keep; it != next.end(); ++it)
                    if (it->hasSpin && (bestSpin == next.end() || better(*it, *bestSpin))) bestSpin = it;
                if (bestSpin != next.end()) std::iter_swap(next.begin() + keep - 1, bestSpin);
            }
            for (auto it = next.begin() + keep; it != next.end(); ++it)
                local_.wait[slot - 1].push(it->node);
            next.resize(keep);
            layer = std::move(next);
            --slot;
        }
    }

    template<bool Hold>
    bool probe_opportunity(Clock::time_point deadline) {
        const auto levels = local_.max_length;
        for (std::size_t attempt = 0; attempt < levels; ++attempt) {
            // Round-robin depths; don't spend the entire reserve widening roots.
            const auto slot = levels - (slotCursor_++ % levels);
            auto& queue = local_.wait[slot];
            if (queue.empty()) continue;
            auto* node = queue.top();
            const auto& state = node->status.get_raw();
            if (state.dead || state.risen != 0) continue;
            queue.pop(); ++stats_.probes;
            expand_opportunity<Hold>(node, slot, deadline);
            return true;
        }
        return false;
    }

    template<bool Hold>
    typename Native::RunResult run_impl(const m_tetris::TetrisMap& map,
        const m_tetris::TetrisNode* node, char held, bool holdFree,
        const char* next, std::size_t length, int milliseconds) {
        stats_ = {}; slotCursor_ = 0;
        if (!context_ || !node || !node->check(map)) return {};
        const auto started = Clock::now();
        const auto duration = std::chrono::milliseconds(std::max(0, milliseconds));
        const auto deadline = started + duration;
        const auto normalDeadline = started + duration * (100 - quotaPercent_) / 100;
        if constexpr (Hold) root_ = root_->update(&local_, map, status_, node, held, !holdFree, next, length);
        else root_ = root_->update(&local_, map, status_, node, next, length);
        bool complete = false;
        do {
            ++stats_.normalPasses;
            complete = root_->template run<Hold>(&local_);
            if (complete) break;
        } while (Clock::now() < normalDeadline);
        if (!complete && quotaPercent_ && milliseconds > 0) {
            const auto quotaStarted = Clock::now();
            const auto quotaDeadline = std::min(deadline, quotaStarted + duration * quotaPercent_ / 100);
            while (Clock::now() < quotaDeadline && probe_opportunity<Hold>(quotaDeadline)) {}
            stats_.quotaMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - quotaStarted).count();
        }
        // If no eligible opportunity exists, normal search gets the unused slice.
        while (!complete && milliseconds > 0 && Clock::now() < deadline) {
            ++stats_.normalPasses;
            complete = root_->template run<Hold>(&local_);
        }
        stats_.totalMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        if constexpr (Hold) {
            if (root_->hold == ' ' && local_.next.size() == 1 && !root_->is_hold_lock)
                return typename Native::RunResult(true);
        }
        const auto best = root_->get_best(&local_);
        return best.first ? typename Native::RunResult(best, Hold && best.first->is_hold)
                          : typename Native::RunResult(false);
    }

public:
    using Status = Evaluation::Status;
    using RunResult = Native::RunResult;
    struct Statistics {
        std::size_t normalPasses = 0, probes = 0, extraExpansions = 0,
                    verifiedSpinExits = 0, continuationExpansions = 0,
                    preparatoryExpansions = 0, modelLeaves = 0, peakBeam = 0;
        double quotaMilliseconds = 0, totalMilliseconds = 0;
    };
private:
    Statistics stats_;
public:
    explicit SpinBeamQuotaEngine(std::shared_ptr<m_tetris::TetrisContext> context = {}, int quotaPercent = 20)
        : context_(std::move(context)), quotaPercent_(quotaPercent) {
        if (quotaPercent < 0 || quotaPercent > 20) throw std::runtime_error("Invalid Spin quota");
        nodes_.emplace_back(); root_ = &nodes_.back();
        if (context_) {
            if (!m_tetris::TetrisRuleInit<rule_tetr_srs_plus::TetrisRule>::init(context_->width(), context_->height()))
                throw std::runtime_error("Invalid shared Spin-quota context");
            bind();
        }
    }
    SpinBeamQuotaEngine(const SpinBeamQuotaEngine&) = delete;
    SpinBeamQuotaEngine& operator=(const SpinBeamQuotaEngine&) = delete;
    bool prepare(int width, int height) {
        if (context_ && context_->width() == width && context_->height() == height) return true;
        Native geometry;
        if (!geometry.prepare(width, height)) return false;
        context_ = geometry.context(); bind(); return true;
    }
    const auto& context() const { return context_; }
    Status* status() { return &status_; }
    Evaluation* ai() { return &ai_; }
    auto* search_config() { return local_.search_config(); }
    void memory_limit(std::uint64_t bytes) { memoryLimit_ = std::clamp<std::uint64_t>(bytes, 32ull << 20, 2ull << 30); }
    std::uint64_t memory_limit() const { return memoryLimit_; }
    const Statistics& statistics() const { return stats_; }
    void update() { local_.is_complete = false; root_->update_version(&local_); }
    RunResult run(const m_tetris::TetrisMap& map, const m_tetris::TetrisNode* node,
                  const char* next, std::size_t length, int milliseconds = 100) {
        return run_impl<false>(map, node, ' ', false, next, length, milliseconds);
    }
    RunResult run_hold(const m_tetris::TetrisMap& map, const m_tetris::TetrisNode* node,
                      char held, bool holdFree, const char* next, std::size_t length, int milliseconds = 100) {
        return run_impl<true>(map, node, held, holdFree, next, length, milliseconds);
    }
};
}
#endif
