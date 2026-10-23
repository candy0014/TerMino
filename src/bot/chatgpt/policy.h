#ifndef TETR_BOT_CHATGPT_POLICY_H
#define TETR_BOT_CHATGPT_POLICY_H

#include <algorithm>
#include <array>
#include <cstdint>
#include "tetr/runtime/spin_bonuses.h"

namespace Bot_chatgpt {

inline bool full_non_t_spins(Runtime::SpinBonuses bonuses) {
    return bonuses==Runtime::SpinBonuses::AllSpin || bonuses==Runtime::SpinBonuses::AllSpinPlus ||
        bonuses==Runtime::SpinBonuses::Handheld || bonuses==Runtime::SpinBonuses::Stupid;
}

struct PressureSnapshot {
    int roof = 0;
    int garbageHeight = 0;
    int incoming = 0;
    int readyIncoming = 0;
};

struct PolicyWeights {
    double attack = 560;
    double b2b = 480;
    double garbageClear = 240;
    double digPressure = 0;
    // Terminal opportunity value, not fabricated attack or a cumulative bonus.
    double comboContinuation = 0; // Retained candidate B; not used by candidate C.
    double tWaste = 140;
    double iWaste = 70;
    double holdReserve = 80;
    // Productive Spin clears deserve continuation work when every family can
    // earn a full bonus. Zero-line rotations earn no completion reward.
    double spinClear = 0;
};

// Shared by native search and the QP rule simulator. Modifier support must
// retain the bot's attack/terrain tradeoff and piece conservation policy.
inline double placement_reward(const PolicyWeights& policy, int attack,
    int garbageCleared, int lines, int family, bool spin, bool perfectClear,
    double delays) {
    double reward=policy.attack*attack+policy.garbageClear*garbageCleared-40.0*lines-120.0*delays;
    if(spin && lines>0)reward+=policy.spinClear*(1+.25*std::min(2,lines-1));
    if(family==5 && !spin && !perfectClear)reward-=policy.tWaste;
    if(family==0 && lines>0 && lines<4 && !perfectClear)reward-=policy.iWaste;
    return reward;
}

// Actual capabilities and visible pressure, independent of modifier names.
// Freeze this at the root so leaf nodes cannot change their valuation style.
inline PolicyWeights capability_policy(const PressureSnapshot& visible,int activeHeight,
    bool canHold,bool nonTFullSpins) {
    PolicyWeights policy;
    const double load=std::clamp((visible.roof+std::min(8,visible.incoming)-activeHeight*.45)/
        std::max(4.,activeHeight*.4),0.,1.);
    const double digging=std::clamp((visible.garbageHeight-activeHeight*.3)/
        std::max(4.,activeHeight*.3),0.,1.);
    policy.garbageClear+=160*std::max(load,digging);
    policy.digPressure=std::max(load,digging);
    if(nonTFullSpins) {policy.tWaste=70;policy.holdReserve=60;policy.spinClear=3000;}
    if(!canHold) {policy.tWaste*=.8;policy.iWaste*=.8;policy.holdReserve=0;}
    return policy;
}

struct TerrainCapabilities {
    bool canHold=true;
    bool constrainedMovement=false;
    bool narrowPieceAvailable=true;
    int largestPiece=4;
};

// Additional costs for physical constraints. Rows are bottom-first, excluding
// the unplayable floor. No modifier or game-mode identifiers enter this score.
inline double capability_terrain(const std::uint32_t* rows,int count,int width,
    int garbageHeight,const TerrainCapabilities& capabilities) {
    int top=count;while(top && !rows[top-1])--top;
    std::array<int,32> heights{};
    const auto full=width==32?~std::uint32_t{}:(std::uint32_t{1}<<width)-1;
    std::uint32_t covered=0;
    double cost=0;
    for(int y=top-1;y>=0;--y) {
        covered|=rows[y];
        for(int x=0;x<width;++x) {
            const auto bit=std::uint32_t{1}<<x;
            if(!heights[x] && (rows[y]&bit))heights[x]=y+1;
            if(capabilities.constrainedMovement && y>=garbageHeight && (covered&~rows[y]&bit))cost+=110;
        }
    }
    if(!capabilities.canHold && !capabilities.narrowPieceAvailable)for(int x=0;x<width;++x) {
        const int left=x?heights[x-1]:top+4,right=x+1<width?heights[x+1]:top+4;
        const int depth=std::max(0,std::min(left,right)-heights[x]-2);
        cost+=35*depth*(depth+1);
    }
    // Filling a covered garbage exit first requires removing its overburden.
    // Use the highest actual incomplete garbage row; lower holes are exposed
    // by digging rather than by trying to fill them through solid material.
    for(int y=std::min(garbageHeight,top)-1;y>=0;--y) {
        const auto exits=full&~rows[y];if(!exits)continue;
        for(int x=0;x<width;++x)if(exits&(std::uint32_t{1}<<x)) {
            int cover=0;for(int above=y+1;above<top;++above)cover+=(rows[above]>>x)&1;
            cost+=80*cover;
        }
        break;
    }
    if(capabilities.largestPiece>4) {
        // Large pieces consume reserve space quickly. A bounded extra height
        // cost gives their next spawn/rotation room without naming a pack.
        cost+=20*(capabilities.largestPiece-4)*top;
    }
    return -cost;
}

inline double combo_reserve(const PolicyWeights& policy, int combo, int occupiedCells) {
    return occupiedCells > 0 ? policy.comboContinuation * std::clamp(combo, 0, 8) : 0.0;
}

// EXPERIMENTAL, not enabled by the host entry point. Retain the strongest
// tested digging blend for further work: it reduced digging pieces but did
// not pass the survival regression gate. Selection has no mode/test inputs.
// The fire endpoint is v0.3; the economical endpoint uses the earlier
// attack/B2B weights plus a stronger garbage-clear reward. Both share Rules
// and legality checks. Clearing urgency is distinct from physical height.
inline PolicyWeights experimental_policy_for(const PressureSnapshot& visible) {
    const double load = std::max(0, visible.roof - 6) +
        std::clamp(visible.readyIncoming, 0, 8) +
        0.5 * std::clamp(visible.incoming - visible.readyIncoming, 0, 8) +
        1.5 * std::clamp(visible.garbageHeight, 0, 8);
    const double pressure = std::clamp((load - 2.0) / 8.0, 0.0, 1.0);
    return {560.0 - 140.0 * pressure, 480.0 - 120.0 * pressure,
            240.0 + 240.0 * pressure, pressure};
}

} // namespace Bot_chatgpt
#endif
