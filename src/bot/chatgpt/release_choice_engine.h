#ifndef TETR_BOT_CHATGPT_RELEASE_CHOICE_ENGINE_H
#define TETR_BOT_CHATGPT_RELEASE_CHOICE_ENGINE_H

#include "evaluation.h"
#include "release_choice_search.h"
#include "variant_tree_node.h"
#include "rule_tetr_srs_plus.h"

namespace Bot_chatgpt {
// Native widening and public tree interface, with variant-aware reuse confined
// to this experimental bot. The shared upstream engine and opponents are intact.
template<class Eval = Evaluation, class SearchType = ReleaseChoiceSearch>
class VariantCacheEngine {
    using Native = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, Eval, SearchType>;
    using Node = variant_cache_detail::VariantTreeNode<typename Eval::Status, Eval, SearchType>;
    using Builder = m_tetris::LocalContextBuilder<typename Node::Context,
        rule_tetr_srs_plus::TetrisRule, Eval, SearchType>;
    using Clock = std::chrono::high_resolution_clock;
    std::deque<Node> nodes_;
    std::shared_ptr<m_tetris::TetrisContext> context_;
    typename Builder::template LocalContext<Node> local_{&nodes_};
    Node* root_ = nullptr;
    Eval ai_;
    SearchType search_;
    typename Eval::Status status_;
    std::uint64_t memoryLimit_ = 128ull << 20;

    void bind() {
        local_.engine = context_.get(); local_.ai = &ai_; local_.search = &search_;
        Builder::init_ai(ai_, &local_, context_.get());
        Builder::init_search(search_, &local_, context_.get());
    }
public:
    using Status = typename Eval::Status;
    struct RunResult { Landing target; Status status; bool change_hold = false; };
private:
    template<bool Hold>
    RunResult run_impl(const m_tetris::TetrisMap& map, const m_tetris::TetrisNode* node,
                       char held, bool holdFree, const char* next, std::size_t length, int ms) {
        if (!context_ || !node || !node->check(map)) return {};
        const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
        if constexpr (Hold) root_ = root_->update(&local_, map, status_, node, held, !holdFree, next, length);
        else root_ = root_->update(&local_, map, status_, node, next, length);
        do { if (root_->template run<Hold>(&local_)) break; } while (Clock::now() < deadline);
        RunResult result;
        if constexpr (Hold) if (root_->hold == ' ' && local_.next.size() == 1 && !root_->is_hold_lock) {
            result.change_hold = true; return result;
        }
        const auto best = root_->get_best(&local_);
        if (best.first) {
            result.target = best.first->identity; result.status = *best.second;
            result.change_hold = Hold && best.first->is_hold;
        }
        return result;
    }
public:
    explicit VariantCacheEngine(std::shared_ptr<m_tetris::TetrisContext> context = {})
        : context_(std::move(context)) {
        nodes_.emplace_back(); root_ = &nodes_.back(); if (context_) bind();
    }
    VariantCacheEngine(const VariantCacheEngine&) = delete;
    VariantCacheEngine& operator=(const VariantCacheEngine&) = delete;
    bool prepare(int width, int height) {
        if (context_ && context_->width() == width && context_->height() == height) return true;
        Native geometry; if (!geometry.prepare(width, height)) return false;
        context_ = geometry.context(); bind(); return true;
    }
    const auto& context() const { return context_; }
    Status* status() { return &status_; }
    Eval* ai() { return &ai_; }
    auto* search_config() { return local_.search_config(); }
    void memory_limit(std::uint64_t limit) { memoryLimit_ = std::clamp<std::uint64_t>(limit, 32ull << 20, 2ull << 30); }
    auto memory_limit() const { return memoryLimit_; }
    std::uint64_t memory_usage() const {
        return context_->node_max() * sizeof(m_tetris::TetrisNode)
            + (nodes_.size() - local_.free_count) * sizeof(Node);
    }
    void update() { local_.is_complete = false; root_->update_version(&local_); }
    RunResult run(const m_tetris::TetrisMap& map, const m_tetris::TetrisNode* node,
                  const char* next, std::size_t length, int ms = 100) {
        return run_impl<false>(map, node, ' ', false, next, length, ms);
    }
    RunResult run_hold(const m_tetris::TetrisMap& map, const m_tetris::TetrisNode* node,
                       char held, bool free, const char* next, std::size_t length, int ms = 100) {
        return run_impl<true>(map, node, held, free, next, length, ms);
    }
};
using ReleaseChoiceEngine = VariantCacheEngine<>;
}
#endif
