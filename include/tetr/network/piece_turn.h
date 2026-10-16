#ifndef TETR_NETWORK_PIECE_TURN_H
#define TETR_NETWORK_PIECE_TURN_H

#include "tetr/runtime/time.h"
#include "tetr/runtime/piece_turn.h"

#include <cstdint>
#include <vector>
#include <map>

namespace Network {

struct PieceTurnState {
    bool enabled = false;
    int playerIndex = 0;
    std::uint32_t epoch = 0;
    std::uint64_t completed = 0;
    std::uint64_t peerCompleted = 0;
    double piecesPerTurn = 1.0;
    Runtime::TimePoint lastSent{};
    std::vector<int> participants;
    std::map<int,std::uint64_t> peers;

    bool can_play() const noexcept {
        if(enabled && !participants.empty()) {
            for(int slot:participants) if(slot!=playerIndex) {
                const auto it=peers.find(slot);const auto value=it==peers.end()?0:it->second;
                if(slot<playerIndex?value<=completed:value<completed) return false;
            }
            return true;
        }
        return !enabled || (playerIndex == 0 ? peerCompleted >= completed
                                            : peerCompleted > completed);
    }
    void complete_pieces(std::uint64_t pieces) noexcept {
        // A completed piece can cover many future, empty turns. Advertise
        // all of them at once, so tiny quotas never require empty-turn loops.
        completed = Runtime::completed_piece_turns(pieces, piecesPerTurn);
    }
};

} // namespace Network
#endif
