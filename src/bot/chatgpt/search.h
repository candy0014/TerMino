#ifndef TETR_BOT_CHATGPT_SEARCH_H
#define TETR_BOT_CHATGPT_SEARCH_H

#include "tetris_core.h"
#include "tetr/rules/spin.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Bot_chatgpt {

// Keep a 25 ms margin below the 125 ms/piece (8 PPS) throughput ceiling.
// Width 150 retains the original 50 ms fast profile; 300 uses 100 ms.
inline int search_budget_ms(int width) {
    return std::clamp(width, 3, 300) / 3;
}

inline int piece_index(char type) {
    const std::string_view names = "IJLOSTZ";
    const auto index = names.find(type);
    return index == names.npos ? -1 : static_cast<int>(index);
}

inline Piece::Pose host_pose(const m_tetris::TetrisNode& node) {
    const auto& shape = Piece::catalog_entry(Piece::Type::from_index(piece_index(node.status.t)))
        .shapes[node.status.r];
    int maxRow = -4, minCol = 4;
    for (const auto& cell : shape) {
        maxRow = std::max(maxRow, cell[0]);
        minCol = std::min(minCol, cell[1]);
    }
    return {{19 - node.row - maxRow, node.col - minCol},
            Runtime::Rotation::from_index(node.status.r)};
}

// Derive the inverse from an actual graph node, including I/O rotation offsets.
inline const m_tetris::TetrisNode* native_pose(const m_tetris::TetrisContext* context,
                                             char type, Piece::Pose pose) {
    const auto* anchor = context->get(type, 3, 22, pose.rotation.index());
    if (!anchor) return nullptr;
    const auto origin = host_pose(*anchor);
    const int x = anchor->status.x + pose.position.col - origin.position.col;
    const int y = anchor->status.y - pose.position.row + origin.position.row;
    if (x < -4 || x > 10 || y < 0 || y >= 44) return nullptr;
    return context->get(type, x, y, pose.rotation.index());
}

// Host spawn/clutch geometry; risen rows are a conservative solid unknown
// substrate, not invented garbage holes. Clutch is legal only after a clear.
inline const m_tetris::TetrisNode* spawn_node(const m_tetris::TetrisContext* context,
                                            const m_tetris::TetrisMap& map, char type,
                                            bool allowClutch, int risen = 0) {
    auto* node = context->generate(type);
    const auto fits = [&](const m_tetris::TetrisNode* candidate) {
        if (!candidate) return false;
        for (int y = 0; y < candidate->height; ++y) {
            const int row = candidate->row + y - risen;
            if (row < 0 || row >= map.height || (map.row[row] & candidate->data[y])) return false;
        }
        return true;
    };
    if (fits(node)) return node;
    if (!allowClutch) return nullptr;
    while (node && (node = node->move_up)) if (fits(node)) return node;
    return nullptr;
}

struct Landing {
    const m_tetris::TetrisNode* node = nullptr;
    bool spin = false;
    bool mini = false;
    double movementSeconds = 0;
    int kick = 0; // 0 means translation; otherwise host kick index + 1.
    Landing() = default;
    Landing(const m_tetris::TetrisNode* value) : node(value) {}
    operator const m_tetris::TetrisNode*() const { return node; }
    const m_tetris::TetrisNode* operator->() const { return node; }
    bool operator==(const Landing& other) const {
        return node == other.node && spin == other.spin && mini == other.mini && kick == other.kick;
    }
    bool operator==(std::nullptr_t) const { return node == nullptr; }
    bool operator!=(std::nullptr_t) const { return node != nullptr; }
};

template<class Config>
inline Landing classify_landing(const m_tetris::TetrisNode* node, int kick,
    const m_tetris::TetrisMap& map, const m_tetris::TetrisMapSnap& snap, const Config* config) {
    Landing landing(node);
    landing.kick = kick;
    if (kick) {
        Rules::SpinInput input;
        input.pose = host_pose(*node);
        input.spinChecker = node->status.t == 'T'
            ? Piece::SpinCheck::TCorner : Piece::SpinCheck::Immobility;
        if (config) {
            const auto metadata = Piece::metadata(config->pieceTypes[piece_index(node->status.t)]);
            input.spinChecker = metadata.spinCheck;
            input.canonicalGeometry = metadata.tetrioRotation;
            input.bonuses = config->spinBonuses;
        }
        input.lastRotation = Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(kick - 1));
        input.mapWidth = 10;
        input.mapHeight = 20;
        input.immobile = (!node->move_up || !node->move_up->check(snap)) &&
            (!node->move_left || !node->move_left->check(snap)) &&
            (!node->move_right || !node->move_right->check(snap));
        const auto spin = Rules::detect_spin(input, [&](int row, int col) {
            const int y = 19 - row;
            return y >= 0 && y < 40 && col >= 0 && col < 10 && map.full(col, y);
        });
        landing.spin = spin.spin;
        landing.mini = spin.mini;
    }
    return landing;
}

// Uses zzztoj's precomputed SRS+ graph, but preserves actual last-rotation
// eligibility and classifies spins with the host's rule implementation.
class Search {
public:
    struct Config {
        bool rootPreviousCleared = false;
        bool allowPartialDescent = true;
        bool instantGravity = false;
        bool wallOnlyHorizontal = false;
        Runtime::SpinBonuses spinBonuses = Runtime::SpinBonuses::AllMiniPlus;
        std::array<Piece::Type,7> pieceTypes{Piece::Type{0},Piece::Type{1},Piece::Type{2},Piece::Type{3},Piece::Type{4},Piece::Type{5},Piece::Type{6}};
    };

    void init(const m_tetris::TetrisContext* context, const Config* config) {
        context_ = context;
        config_ = config;
        marks_.resize(context->node_max());
        landingMarks_.resize(context->node_max());
        landingIndices_.resize(context->node_max());
    }

    const std::vector<Landing>* search(const m_tetris::TetrisMap& map,
                                      const m_tetris::TetrisNode* start, std::size_t depth) {
        landings_.clear();
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
            ++generation_;
        }
        m_tetris::TetrisMapSnap snap;
        start->build_snap(map, context_, snap);
        // Above all obstacles every horizontal/rotation transition is open.
        while ((!config_ || !config_->instantGravity) && (!config_ || config_->allowPartialDescent) && start->low >= map.roof && start->move_down && start->move_down->check(snap))
            start = start->move_down;
        auto enqueue = [&](const m_tetris::TetrisNode* node, int kick) {
            if (!node || !node->check(snap)) return;
            if(config_ && config_->instantGravity) while(node->move_down && node->move_down->check(snap)) {
                node=node->move_down;kick=0;
            }
            if (!node->move_down || !node->move_down->check(snap)) {
                Landing landing = classify_landing(node, kick, map, snap, config_);
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
                marks_[node->index] = generation_;
                queue_.push_back(node);
            }
        };
        const auto shift=[&](const m_tetris::TetrisNode* node,bool right) {
            auto next=right ? node->move_right : node->move_left;
            if(!config_ || !config_->instantGravity || !config_->wallOnlyHorizontal) {enqueue(next,0);return;}
            while(next && next->check(snap)) {
                node=next;
                while(node->move_down && node->move_down->check(snap)) node=node->move_down;
                next=right ? node->move_right : node->move_left;
            }
            enqueue(node,0);
        };
        enqueue(start, 0);
        if(config_ && config_->instantGravity && marks_[start->index]!=generation_) {
            // HOLD/future spawn may buffer one turn or shift before its first
            // instant fall. Later states always remain on the surface.
            marks_[start->index]=generation_;queue_.push_back(start);
        }
        for (std::size_t i = 0; i < queue_.size(); ++i) {
            const auto* node = queue_[i];
            if (!config_ || (!config_->instantGravity && config_->allowPartialDescent)) enqueue(node->move_down,0);
            else if(!config_->instantGravity) {
                const auto* dropped=node;
                while(dropped->move_down && dropped->move_down->check(snap))dropped=dropped->move_down;
                if(dropped!=node)enqueue(dropped,0);
            }
            shift(node,false);shift(node,true);
            for (const auto* kicks : {node->wall_kick_clockwise,
                                      node->wall_kick_counterclockwise, node->wall_kick_opposite}) {
                for (int k = 0; k < m_tetris::max_wall_kick && kicks[k]; ++k) {
                    if (!kicks[k]->check(snap)) continue;
                    enqueue(kicks[k], k + 1);
                    if(config_ && config_->instantGravity) {shift(kicks[k],false);shift(kicks[k],true);}
                    break; // SRS uses the first successful kick only.
                }
            }
        }
        return &landings_;
    }

private:
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
