#ifndef TETR_BOT_CHATGPT_RELEASE_CHOICE_SEARCH_H
#define TETR_BOT_CHATGPT_RELEASE_CHOICE_SEARCH_H
#include "search.h"
namespace Bot_chatgpt {
// Uses zzztoj's precomputed SRS+ graph, but preserves actual last-rotation
// eligibility and classifies spins with the host's rule implementation.
class ReleaseChoiceSearch {
public:
    using Config = Search::Config;

    void init(const m_tetris::TetrisContext* context, const Config* config) {
        instantSearch_.init(context,config);
        context_ = context;
        config_ = config;
        marks_.resize(context->node_max());
        landingMarks_.resize(context->node_max());
        landingIndices_.resize(context->node_max());
        releaseChecked_.resize(context->node_max());
    }

    const std::vector<Landing>* search(const m_tetris::TetrisMap& map,
                                      const m_tetris::TetrisNode* start, std::size_t depth) {
        if(config_ && config_->instantGravity) return instantSearch_.search(map,start,depth);
        landings_.clear();
        plainReleases_.clear();
        queue_.clear();
        if (!start) return &landings_;
        if (!start->check(map)) {
            // Future branches are additionally checked by Evaluation using
            // their exact previous-clear state. At root this is a HOLD spawn.
            start = spawn_node(context_, map, start->status.t,
                               depth > 0 || (config_ && config_->rootPreviousCleared));
            if (!start) return &landings_;
        }
        if (++generation_ == 0) {
            std::fill(marks_.begin(), marks_.end(), 0);
            std::fill(landingMarks_.begin(), landingMarks_.end(), 0);
            std::fill(releaseChecked_.begin(), releaseChecked_.end(), 0);
            ++generation_;
        }
        m_tetris::TetrisMapSnap snap;
        start->build_snap(map, context_, snap);
        // Above all obstacles every horizontal/rotation transition is open.
        while ((!config_ || config_->allowPartialDescent) && start->low >= map.roof &&
               start->move_down && start->move_down->check(snap))
            start = start->move_down;
        auto enqueue = [&](const m_tetris::TetrisNode* node, int kick) {
            if (!node || !node->check(snap)) return;
            if (!node->move_down || !node->move_down->check(snap)) {
                Landing landing = classify_landing(node, kick, map, snap, config_);
                const auto index = node->index_filtered;
                if (landingMarks_[index] != generation_) {
                    landingMarks_[index] = generation_;
                    landingIndices_[index] = landings_.size();
                    landings_.push_back(landing);
                } else {
                    auto& previous = landings_[landingIndices_[index]];
                    // Preserve the original strongest-variant order. Only a
                    // real plain/Spin collision can add a B2B release option.
                    if(node->status.t=='T' && landing.spin!=previous.spin &&
                       releaseChecked_[index]!=generation_) {
                        releaseChecked_[index]=generation_;
                        int clearRows=0;
                        for(int y=0;y<node->height;++y)
                            clearRows+=(map.row[node->row+y]|node->data[y])==context_->full();
                        if(clearRows>0&&clearRows<4)
                            plainReleases_.push_back(landing.spin?previous:landing);
                    }
                    if (int(landing.spin) * 2 - int(landing.mini) >
                        int(previous.spin) * 2 - int(previous.mini)) previous = landing;
                }
            }
            if (marks_[node->index] != generation_) {
                marks_[node->index] = generation_;
                queue_.push_back(node);
            }
        };
        enqueue(start, 0);
        for (std::size_t i = 0; i < queue_.size(); ++i) {
            const auto* node = queue_[i];
            if (!config_ || config_->allowPartialDescent) enqueue(node->move_down, 0);
            else {
                const auto* dropped = node;
                while (dropped->move_down && dropped->move_down->check(snap)) dropped = dropped->move_down;
                if (dropped != node) enqueue(dropped, 0);
            }
            enqueue(node->move_left, 0);
            enqueue(node->move_right, 0);
            for (const auto* kicks : {node->wall_kick_clockwise,
                                      node->wall_kick_counterclockwise, node->wall_kick_opposite}) {
                for (int k = 0; k < m_tetris::max_wall_kick && kicks[k]; ++k) {
                    if (!kicks[k]->check(snap)) continue;
                    enqueue(kicks[k], k + 1);
                    break; // SRS uses the first successful kick only.
                }
            }
        }
        landings_.insert(landings_.end(),plainReleases_.begin(),plainReleases_.end());
        return &landings_;
    }

private:
    Search instantSearch_;
    const m_tetris::TetrisContext* context_ = nullptr;
    const Config* config_ = nullptr;
    std::uint32_t generation_ = 0;
    std::vector<std::uint32_t> marks_, landingMarks_, releaseChecked_;
    std::vector<std::size_t> landingIndices_;
    std::vector<const m_tetris::TetrisNode*> queue_;
    std::vector<Landing> landings_, plainReleases_;
};
}

#endif
