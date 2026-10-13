#ifndef TETR_BOT_CHATGPT_H
#define TETR_BOT_CHATGPT_H

#include "tetr/bot/environment.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/playfield.h"
#include <vector>
#include <array>
#include <utility>
#include "tetr/autoplay/strategy.h"

namespace Bot_chatgpt {
// Read-only diagnostics. Never fed back into the policy or tied to test IDs.
struct Diagnostics {
    int roof = 0, incoming = 0, readyIncoming = 0;
    int fallbackReason = 0; // 0 accepted, 1 no target, 2 type mismatch, 3 unreachable.
    int projectedAttack = 0;
    double value = 0;
    double searchMilliseconds = 0;
    double digPressure = 0; // Visible-state blend: 0 firepower, 1 fast downstack.
    double forecastStepSeconds = 0;
    bool openerDoubleCancelEnabled = false;
    // Exact visible root inputs for offline replay; never read back by the BOT.
    std::vector<int> rootNext;
    std::vector<int> rootUnseen;
    // Separate from rootNext: experimental deduced search-tail piece, or -1.
    int inferredNextPiece = -1;
    std::vector<std::pair<int, double>> rootPackets;
    int rootHold = -2, rootRow = 0, rootCol = 0, rootRotation = 0;
    int rootB2b = 0, rootCombo = 0, rootGarbageHeight = 0;
    bool rootPreviousCleared = false;
    int projectedB2b = 0, projectedRisen = 0, projectedDepth = 0;
    bool projectedDead = false;
    double projectedReward = 0, projectedPathRisk = 0;
    std::array<unsigned, 40> rows{}; // bottom-up board before the decision.
};
Diagnostics last_diagnostics();
std::vector<Autoplay::PlacementCandidate> last_candidates();
bool init(const Bot::Environment& environment);
Autoplay::Availability availability(const Bot::Environment& environment);
int getpos(const Bot::Environment& environment, const Runtime::PlayerSessionState& player,
           Runtime::Playfield field, std::vector<int> next, const std::vector<int>& unseenBagPieces,
           int startRow, int startCol, int startRotation, int hold,
           int& answerRow, int& answerCol, int& answerRotation, int& answerKick);
}

#endif
