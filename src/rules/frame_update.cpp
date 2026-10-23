#include "tetr/rules/frame_update.h"
#include "tetr/rules/quick_play.h"

#include <algorithm>
#include <cmath>

namespace Rules {
namespace {

void generate_survival_packets(
    std::vector<int>& packets,
    Runtime::Duration deltaTime,
    double survivalApm,
    std::mt19937& rng) {
    double remaining = deltaTime.seconds();
    while (remaining > 1e-7) {
        const double stepSeconds = std::min(0.05, remaining);
        remaining -= stepSeconds;
        if (rng() % 3600 < survivalApm * stepSeconds / 0.05) {
            packets.push_back(static_cast<int>(rng() % 5) + 1);
        }
    }
}

void generate_qp_packets(
    std::vector<int>& packets,
    Runtime::Duration deltaTime,
    FrameRuleState& state,
    const FrameRuleConfig& config,
    std::mt19937& rng) {
    // The public endpoint reports received lines, not opponents' gross APM.
    // Generate a line budget with the fitted mean AND variance, then group it
    // into attacks. Independent Poisson packets would multiply the fitted
    // variance by packet size and recreate the excessive burstiness.
    constexpr double weights[][5] = {
        {.8162,.0655,.0064,.1101,.0018}, {.8028,.0663,.0034,.1265,.0011},
        {.7528,.0683,.0081,.1698,.0011}, {.6059,.0873,.0389,.2649,.0030},
        {.4603,.1160,.0750,.3452,.0035}, {.3772,.1352,.0943,.3891,.0043}
    };
    constexpr int sizes[]={1,2,3,5,7};
    const double altitude=std::max(0.0,state.qpAltitude);
    const int band=altitude<150?0:altitude<450?1:altitude<850?2:altitude<1650?3:altitude<3500?4:5;
    const auto pressure=qp_pressure(altitude,config.ownAttackApm);
    const double sigma=std::sqrt(std::log1p(pressure.relativeVariance));
    auto& p=state.qpProgress;
    p.opponentTickRemainder+=std::max(0.0,deltaTime.seconds());
    const auto unit=[&]{return (double(rng())+.5)/(double(std::mt19937::max())+1);};
    const auto next_attack=[&] {
        // Packet mix is an approximation; public score stubs have no events.
        if(unit()<std::clamp(altitude/40000.,.01,.08))return 8+int(rng()%9);
        double sum=0;for(double weight:weights[band])sum+=weight;
        double draw=unit()*sum;
        for(int i=0;i<5;++i){draw-=weights[band][i];if(draw<0)return sizes[i];}
        return sizes[4];
    };
    constexpr double tick=1.0/60;
    while(p.opponentTickRemainder+1e-10>=tick) {
        p.opponentTickRemainder=std::max(0.0,p.opponentTickRemainder-tick);
        if(!p.opponentPressureInitialized) {
            const double u=unit(),v=unit();
            p.opponentPressureNormal=std::sqrt(-2*std::log(u))*std::cos(6.283185307179586*v);
            p.opponentPressureInitialized=true;
        }
        if(!p.opponentNextAttack)p.opponentNextAttack=next_attack();
        const double elapsed=config.elapsedGameTime.seconds()-p.opponentTickRemainder;
        const double fatigue=1+(elapsed>=540?.25:0)+(elapsed>=660?.25:0);
        double rate=pressure.mean*fatigue*std::exp(sigma*p.opponentPressureNormal-.5*sigma*sigma);
        if(!qp_hard_mode(config.qpMods) && config.inDanger)rate*=.5;
        if(Runtime::reverse(config.qpMods.messy) || Runtime::reverse(config.qpMods.doubleHole) ||
            config.qpMods.allSpin==Runtime::AllSpinMode::Reverse)
            rate*=std::max(0.,1-std::min(2.5,config.garbageRows*.5)/3);
        // Quantum q and Bernoulli p give variance/mean q-rate/3600 = 0.93756,
        // the independently fitted shot-noise term. The retained normal draw
        // supplies the altitude/APM-dependent between-run variance.
        const double quantum=0.9375553655037346+rate/3600;
        if(unit()<rate/(quantum*3600))p.opponentLineCredits+=quantum;
        // The fit already includes normal reception and Windup clipping.
        // Account for that cost BEFORE applying real reception once below;
        // Mod effects and transient receive protection remain in shared rules.
        const double adjusted=qp_receive_attack(p.opponentNextAttack,altitude,altitude,
            state.qpFloor,Runtime::QuickPlayConfig{},Runtime::QuickPlayProgress{},elapsed);
        const int lo=int(std::floor(adjusted)),hi=int(std::ceil(adjusted));
        const int capacity=16+int(std::max(0.,std::floor(altitude/500)-7));
        const auto clipped=[&](int attack){return std::min(attack,capacity);};
        const double cost=clipped(lo)*(1-(adjusted-lo))+clipped(hi)*(adjusted-lo);
        if(cost>0 && p.opponentLineCredits+1e-10>=cost) {
            p.opponentLineCredits=std::max(0.,p.opponentLineCredits-cost);
            packets.push_back(p.opponentNextAttack);p.opponentNextAttack=next_attack();
        }
    }
}

} // namespace

FramePrePresentationResult advance_frame_before_presentation(
    FrameRuleState& state,
    const FrameRuleConfig& config,
    Runtime::Duration deltaTime,
    std::mt19937& rng) {
    FramePrePresentationResult result;

    if (config.garbageMode == Runtime::GarbageMode::Survival && !config.replayMode) {
        generate_survival_packets(
            result.incomingGarbagePackets,
            deltaTime,
            state.survivalApm,
            rng);
    }

    if (config.garbageMode == Runtime::GarbageMode::QuickPlay) {
        if (!config.replayMode && config.generateOpponentPressure) {
            generate_qp_packets(
                result.incomingGarbagePackets,
                deltaTime,
                state,
                config,
                rng);
        }

        auto mods = config.qpMods;
        mods.expert = config.qpModExpert;
        mods.gravity = config.qpModGravity;
        auto& p = state.qpProgress;
        const double seconds = std::max(0.0, deltaTime.seconds());
        qp_advance_booster(p, state.qpSpeed, state.qpAltitude, state.qpFloor, mods, seconds);
        if (mods.continuousCancellation) {
            p.sinceRelief += seconds;
            while (p.sinceRelief + 1e-7 >= 30) { p.cancellation += 5; p.sinceRelief -= 30; }
        }
        p.sinceReceived += seconds;
        constexpr double protectionIntervals[] = {4.8,3.9,2.1,1.4,1.3,0.9,0.6,0.4,0.3,0.2};
        const double releaseInterval = Runtime::reverse(mods.expert)
            ? (11 - state.qpFloor) * 0.1 : protectionIntervals[state.qpFloor - 1];
        while (p.receiveProtection > 0 && p.sinceReceived + 1e-7 >= releaseInterval) {
            --p.receiveProtection;
            p.sinceReceived -= releaseInterval;
        }
        const double oldFloorTime = p.floorTime;
        p.floorTime += seconds;
        if (Runtime::reverse(mods.expert))
            p.stallMultiplier += 0.005 * (std::max(0.0, p.floorTime - 60) - std::max(0.0, oldFloorTime - 60));
        const int floor = qp_floor(state.qpAltitude);
        if (floor != state.qpFloor) {
            state.qpBagBreather = true;
            p.floorTime = 0;
            if (mods.gravity == Runtime::ModifierMode::Enabled && floor > state.qpFloor) {
                const double period = state.gravitySpeed.acceleration_seed_period().seconds();
                if (!state.gravitySpeed.is_instant() && !state.gravitySpeed.is_disabled())
                    state.gravitySpeed = Runtime::GravityInterval::periodic(
                        Runtime::Duration::from_seconds(1 / (1 / period + 18 * (floor - state.qpFloor))));
            }
            state.qpFloor = floor;
        }
        const double elapsed = config.elapsedGameTime.seconds();
        const int stage = std::clamp(static_cast<int>(elapsed / 60), 0, 12);
        const int dueSolidLines=qp_fatigue_garbage_due(mods,elapsed);
        result.permanentGarbageLines=std::max(0,dueSolidLines-p.fatigueGarbageLines);
        p.fatigueGarbageLines=std::max(p.fatigueGarbageLines,dueSolidLines);
        p.fatigueStage = std::max(p.fatigueStage, stage);
        for (auto it = p.windups.begin(); it != p.windups.end();) {
            if (it->due <= elapsed + 1e-7) {
                if (!config.replayMode) result.resolvedQpGarbagePackets.push_back(it->attack);
                it = p.windups.erase(it);
            } else ++it;
        }
        p.windupWarnings.erase(std::remove_if(p.windupWarnings.begin(),p.windupWarnings.end(),
            [elapsed](const auto& warning) { return warning.end<=elapsed; }),p.windupWarnings.end());
        if (config.replayMode) result.permanentGarbageLines = 0;
        state.survivalApm = qp_pressure(state.qpAltitude,config.ownAttackApm).mean;
    }

    return result;
}

void advance_frame_after_presentation(
    FrameRuleState& state,
    const FrameRuleConfig& config,
    Runtime::Duration deltaTime) {
    if (config.garbageMode != Runtime::GarbageMode::QuickPlay) return;

    (void)deltaTime;
    auto mods = config.qpMods;
    mods.expert = config.qpModExpert;
    mods.gravity = config.qpModGravity;
    state.garbageInTime = qp_garbage_delay(mods, state.qpFloor, state.qpProgress.cancellation);
    if (Runtime::enabled(mods.gravity)) {
        constexpr int ordinary[] = {30,29,28,27,26,24,22,20,18,16};
        constexpr int reverse[] = {24,22,20,18,16,15,14,13,12,11};
        state.epld = Runtime::Duration::from_seconds((Runtime::reverse(mods.gravity)
            ? reverse : ordinary)[state.qpFloor - 1] / 60.0);
    }
}

} // namespace Rules
