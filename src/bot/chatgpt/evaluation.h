#ifndef TETR_BOT_CHATGPT_EVALUATION_H
#define TETR_BOT_CHATGPT_EVALUATION_H

#include "search.h"
#include "policy.h"
#include "forecast.h"
#include "terrain_features.h"
#include "ai_zzz.h"
#include "tetr/rules/attack.h"
#include "tetr/rules/chain.h"
#include "tetr/rules/opener_phase.h"
#include "tetr/rules/garbage_queue.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <string>

namespace Bot_chatgpt {

class Evaluation {
public:
    struct Result {
        ai_zzz::Botris::Result shape;
        std::uint64_t clearedRows = 0;
        const m_tetris::TetrisMap* before = nullptr;
    };
    struct Status {
        int combo = 0;
        int b2b = 0;
        int garbageHeight = 0;
        int incoming = 0;
        int readyIncoming = 0;
        int risen = 0;
        bool dead = false;
        bool previousCleared = false;
        bool openerDoubleCancelEnabled = false;
        int placedPieces = 0;
        int attack = 0;
        double reward = 0;
        double pathRisk = 0;
        double value = 0;
        int openerPhase = 14;
        bool legacyOpenerPhase = true;
        int sentAttack = 0;
        int garbageCap = 8;
        double sendMultiplier = 1.0;
        double cancellationMultiplier = 1.0;
        Runtime::GarbageCancellationState cancellation;
        Runtime::SpinBonuses spinBonuses = Runtime::SpinBonuses::AllMiniPlus;
        Runtime::AttackRules attackRules;
        std::array<std::uint16_t,40> pieceRows{};
        double elapsedSeconds = 0;
        double entryDelaySeconds = 0;
        double lineClearDelaySeconds = 0;
        bool pieceTime = false;
        double calculationSeconds = 0;
        double rootWaitSeconds = 0;
        bool operator<(const Status& other) const { return value < other.value; }
    };

    void init(const m_tetris::TetrisContext* context) {
        context_ = context;
        terrain_.init(context, nullptr);
    }
    std::string ai_name() const { return "bot_chatgpt v0.3.2-rc1"; }
    double ratio() const { return 1.5; }
    // Used by the offline fixed-root probe; production keeps its frozen value.
    void set_covered_empty_weight(double weight) { coveredEmptyWeight_ = weight; }

    // Freeze weights for all alternatives in one search. Recompute only from
    // the next real root, so a leaf cannot improve its score by switching style.
    void set_policy(const PolicyWeights& policy) { policy_ = policy; }
    void set_garbage_forecast(const std::vector<VisiblePacket>& packets, double secondsPerPiece) {
        forecast_.reset(packets, secondsPerPiece);
    }

    struct Candidate { int type; Piece::Pose pose; Runtime::RotationRecord rotation; double value, movementSeconds; };
    void clear_candidates() { candidates_.clear(); }
    const std::vector<Candidate>& candidates() const { return candidates_; }

    Result eval(const Landing& landing, const m_tetris::TetrisMap& map,
                const m_tetris::TetrisMap& before, std::size_t clear) const {
        Result result;
        result.before = &before;
        // The upstream revision needs a local empty-board guard for row[-1].
        // Handle it here as well, so this BOT is safe with a clean checkout.
        result.shape = map.roof == 0
            ? ai_zzz::Botris::Result{-16000.0, static_cast<int>(clear), 0, &map}
            : terrain_.eval(ai_zzz::Botris::TetrisNodeEx(landing.node), map, before, clear);
        for (int y = 0; y < landing->height; ++y) {
            const int row = landing->row + y;
            if ((before.row[row] | landing->data[y]) == context_->full())
                result.clearedRows |= std::uint64_t{1} << row;
        }
        return result;
    }

    Status get(Landing& landing, const Result& evaluation, std::size_t depth,
               const Status& previous, const m_tetris::TetrisContext::Env& env) const {
        // Death is terminal: subsequent search geometry is not a playable
        // continuation and must not earn attacks or alter the first-death score.
        if (previous.dead) return previous;
        Status result = previous;
        ++result.placedPieces; // Host increments placedBlocks before offset.
        const auto& map = *evaluation.shape.map;
        const int lines = evaluation.shape.clear;
        const double delays = result.entryDelaySeconds + (lines>0 ? result.lineClearDelaySeconds : 0);
        const double beforeSpawn = depth>0 ? result.entryDelaySeconds +
            (previous.previousCleared ? result.lineClearDelaySeconds : 0) : 0;
        // Readiness is measured at lock, before this piece's clear/entry wait.
        // At the real root, those waits have already happened. Piece-time
        // forecasts keep their explicit simulation clock regardless of delays.
        const double work = result.calculationSeconds + beforeSpawn;
        const double cycle = result.pieceTime ? (depth>0 ? forecast_.step_seconds() : 0) :
            std::max(depth==0 ? result.rootWaitSeconds : forecast_.step_seconds(), work);
        result.elapsedSeconds += cycle;
        result.readyIncoming = forecast_.ready_at(result.elapsedSeconds,result.incoming,result.readyIncoming);
        int garbageCleared = 0;
        for (int y = 0; y < std::min(40, previous.garbageHeight); ++y)
            garbageCleared += (evaluation.clearedRows >> y) & 1;
        result.garbageHeight -= garbageCleared;

        const bool emptyField = map.count == 0 && previous.risen == 0;
        bool halfClear = false;
        if (previous.attackRules.halfClearEnabled) {
            for (int y=0; y<landing->height; ++y) {
                const int row=landing->row+y;
                if (row>=0 && row<40) result.pieceRows[row] |= landing->data[y];
            }
            int destination=0;
            for (int y=0; y<40; ++y)
                if (!(evaluation.clearedRows & (std::uint64_t{1}<<y)))
                    result.pieceRows[destination++]=result.pieceRows[y];
            std::fill(result.pieceRows.begin()+destination,result.pieceRows.end(),0);
            halfClear = lines>0 && !emptyField &&
                std::none_of(result.pieceRows.begin(),result.pieceRows.end(),[](auto row){return row!=0;});
        }
        const bool perfectClear = emptyField && previous.attackRules.perfectClearEnabled;
        const auto chain = Rules::update_chain({lines, landing.spin, perfectClear,
            Runtime::AllSpinMode::Disabled, previous.b2b, previous.combo, Runtime::GarbageMode::None, previous.attackRules, halfClear});
        Rules::AttackInput input;
        input.linesCleared = lines;
        input.spin = landing.spin;
        input.mini = landing.mini;
        input.perfectClear = perfectClear;
        input.b2b = chain.b2b;
        input.combo = chain.combo;
        input.previousB2B = chain.previousB2B;
        input.b2bCharging = chain.b2bCharging;
        input.b2bTransition = chain.b2bTransition;
        input.clearedGarbageLines = garbageCleared;
        input.piece.familyIndex = piece_index(landing->status.t);
        // Standard Season 2 rules are identical across all requested tests.
        // No test/mode identifiers or future garbage RNG are read here.
        input.garbageMultiplier = result.sendMultiplier;
        input.spinBonuses = result.spinBonuses;
        input.attackRules = result.attackRules;
        input.halfClear = halfClear;
        const auto attackResult = Rules::calculate_attack(input);
        const int attack = attackResult.attack;
        result.b2b = chain.b2b;
        result.combo = chain.combo;
        result.previousCleared = lines > 0;
        result.attack += attack;
        const int beforeIncoming = result.incoming;
        const int pc = result.legacyOpenerPhase ? 0 : std::min(attack,attackResult.perfectClearAttack + attackResult.halfClearAttack);
        for (const int amount : {attack-pc,pc}) {
            const bool doubled = Rules::opener_phase_double_cancel(result.placedPieces,
                amount,result.incoming,result.openerDoubleCancelEnabled,
                result.openerPhase,result.sentAttack,result.legacyOpenerPhase);
            if(amount<=0)continue;
            const auto cancelled=Rules::cancel_garbage_amount(result.incoming,amount,result.cancellationMultiplier,doubled,result.cancellation);
            result.incoming=static_cast<int>(cancelled.remaining);
            result.sentAttack+=cancelled.outgoingAttack;
        }
        const int cancel = beforeIncoming-result.incoming;
        result.readyIncoming = std::max(0, result.readyIncoming - cancel);
        result.readyIncoming = forecast_.ready_at(result.elapsedSeconds,result.incoming,result.readyIncoming);
        // Clearing stalls garbage. On a non-clear, the known ready FIFO
        // prefix rises up to the configured cap, and can no longer be cancelled
        // by an attack several pieces later. Unknown hole locations are not
        // invented: retain a vertical offset for safety evaluation instead.
        if (!lines) {
            const int rise = std::min(result.garbageCap, result.readyIncoming);
            result.risen += rise;
            result.readyIncoming -= rise;
            result.incoming -= rise;
            if (!result.incoming) result.cancellation.cancellationFraction=0;
        }

        const double discount = std::pow(0.96, static_cast<double>(depth));
        result.reward -= discount * 120.0 * (result.pieceTime ? 0.0 : delays);
        result.reward += discount * (policy_.attack * attack +
            policy_.garbageClear * garbageCleared - 40.0 * lines);
        if (landing->status.t == 'T' && !landing.spin && !perfectClear)
            result.reward -= 140.0 * discount;
        if (landing->status.t == 'I' && lines > 0 && lines < 4 && !perfectClear)
            result.reward -= 70.0 * discount;

        const double danger = std::max(0, map.roof + result.risen + std::min(8, result.incoming) - 12);
        // A low final board must not hide dangerous intermediate positions.
        // Charge each step of the plan for exposed height, even when a later
        // attack/clear could remove that pressure. Only visible state is used.
        result.pathRisk += discount * 35.0 * danger * danger;
        result.value = evaluation.shape.value + result.reward +
            policy_.b2b * result.b2b + (result.b2b ? 300.0 : 0.0) -
            95.0 * danger * danger - result.pathRisk;
        if (coveredEmptyWeight_ != 0)
            result.value -= coveredEmptyWeight_ * covered_empty_cells(map);
        // A live combo offers cheaper continued output/cancellation. Keep a
        // bounded terminal estimate so finite preview does not always prune
        // low-attack setup clears. It disappears on a non-clear/empty board;
        // it never changes real attack, and is not accumulated every step.
        result.value += discount * combo_reserve(policy_, result.combo, map.count);
        if (env.hold == 'T' || env.hold == 'I') result.value += 80;
        // The host has spawn-out, not an unconditional above-visible lock-out.
        // Clearing can unlock Clutch for the next spawn; HOLD uses the previous
        // piece's clear state, not the clear this candidate is about to make.
        result.dead = previous.dead;
        if (env.is_hold && evaluation.before &&
            !spawn_node(context_, *evaluation.before, landing->status.t,
                        previous.previousCleared, previous.risen)) result.dead = true;
        if (env.length) {
            if (!spawn_node(context_, map, *env.next, lines > 0, result.risen)) result.dead = true;
        } else if (map.roof + result.risen >= 40) {
            result.dead = true;
        }
        if (result.dead) result.value -= 1e9;
        if (depth==0) {
            const auto rotation = landing.kick ? Runtime::RotationRecord::from_kick(
                Piece::KickIndex::from_index(landing.kick-1)) : Runtime::RotationRecord::none();
            candidates_.push_back({piece_index(landing->status.t),
                host_pose(*landing.node),rotation,result.value,0});
        }
        return result;
    }

private:
    mutable std::vector<Candidate> candidates_;
    PolicyWeights policy_;
    GarbageForecast forecast_;
    const m_tetris::TetrisContext* context_ = nullptr;
    ai_zzz::Botris terrain_;
    // Stage 8's 80/cell candidate failed the development score gate. Keep the
    // original policy; the offline root probe explicitly tests both values.
    double coveredEmptyWeight_ = 0;
};
}

#endif
