#ifndef TETR_BOT_CHATGPT_COVERAGE_SEARCH_H
#define TETR_BOT_CHATGPT_COVERAGE_SEARCH_H

#include "search.h"

namespace Bot_chatgpt {

// P5-A: explore the movement graph from the actual entry instead of first
// dropping along the spawn column. An early descent can irreversibly remove
// high horizontal/rotation routes. Keep the original geometry deduplication,
// strongest-Spin preference, SRS first-success rule and scoring interface.
// The extra enumeration work shares the unchanged wall-clock search budget.
class CoverageSearch {
public:
    using Config = Search::Config;
    void init(const m_tetris::TetrisContext* context, const Config* config) {
        instantSearch_.init(context,config);
        context_ = context; config_ = config;
        marks_.resize(context->node_max());
        landingMarks_.resize(context->node_max());
        landingIndices_.resize(context->node_max());
    }

    const std::vector<Landing>* search(const m_tetris::TetrisMap& map,
                                      const m_tetris::TetrisNode* start, std::size_t depth) {
        if(config_ && config_->instantGravity) return instantSearch_.search(map,start,depth);
        landings_.clear(); queue_.clear();
        if (!start) return &landings_;
        if (!start->check(map)) {
            start = spawn_node(context_, map, start->status.t,
                               depth > 0 || (config_ && config_->rootPreviousCleared));
            if (!start) return &landings_;
        }
        if (++generation_ == 0) {
            std::fill(marks_.begin(), marks_.end(), 0);
            std::fill(landingMarks_.begin(), landingMarks_.end(), 0);
            ++generation_;
        }
        m_tetris::TetrisMapSnap snap;
        start->build_snap(map, context_, snap);
        const auto fits = [&](const m_tetris::TetrisNode* node) {
            return node && node->row >= 0 && node->row + node->height <= map.height && node->check(snap);
        };
        const auto enqueue = [&](const m_tetris::TetrisNode* node, int kick) {
            if (!fits(node)) return;
            if (!fits(node->move_down)) {
                Landing landing(node); landing.kick = kick;
                if (kick) {
                    Rules::SpinInput input;
                    input.pose = host_pose(*node);
                    input.spinChecker = node->status.t == 'T'
                        ? Piece::SpinCheck::TCorner : Piece::SpinCheck::Immobility;
                    input.lastRotation = Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(kick - 1));
                    input.mapWidth = 10; input.mapHeight = 20;
                    input.immobile = !fits(node->move_up) && !fits(node->move_left) && !fits(node->move_right);
                    const auto spin = Rules::detect_spin(input, [&](int row, int col) {
                        const int y = 19 - row;
                        return y >= 0 && y < 40 && col >= 0 && col < 10 && map.full(col, y);
                    });
                    landing.spin = spin.spin; landing.mini = spin.mini;
                }
                const auto index = node->index_filtered;
                if (landingMarks_[index] != generation_) {
                    landingMarks_[index] = generation_;
                    landingIndices_[index] = landings_.size();
                    landings_.push_back(landing);
                } else {
                    auto& previous = landings_[landingIndices_[index]];
                    if (int(landing.spin) * 2 - int(landing.mini) >
                        int(previous.spin) * 2 - int(previous.mini)) previous = landing;
                }
            }
            if (marks_[node->index] != generation_) {
                marks_[node->index] = generation_; queue_.push_back(node);
            }
        };
        enqueue(start, 0);
        for (std::size_t i = 0; i < queue_.size(); ++i) {
            const auto* node = queue_[i];
            enqueue(node->move_down, 0);
            enqueue(node->move_left, 0);
            enqueue(node->move_right, 0);
            for (const auto* kicks : {node->wall_kick_clockwise,
                                      node->wall_kick_counterclockwise, node->wall_kick_opposite}) {
                for (int k = 0; k < m_tetris::max_wall_kick && kicks[k]; ++k) {
                    if (!fits(kicks[k])) continue;
                    enqueue(kicks[k], k + 1);
                    break;
                }
            }
        }
        return &landings_;
    }

private:
    Search instantSearch_;
    const m_tetris::TetrisContext* context_ = nullptr;
    const Config* config_ = nullptr;
    std::uint32_t generation_ = 0;
    std::vector<std::uint32_t> marks_, landingMarks_;
    std::vector<std::size_t> landingIndices_;
    std::vector<const m_tetris::TetrisNode*> queue_;
    std::vector<Landing> landings_;
};
}
#endif
