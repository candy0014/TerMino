#include "tetr/rules/quick_play.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Rules {
namespace {
constexpr double floors[] = {0, 50, 150, 300, 450, 650, 850, 1100, 1350, 1650};

bool new_column(std::array<int, 2>& history, int column) {
    if (column == history[0] || column == history[1]) return false;
    history[1] = history[0];
    history[0] = column;
    return true;
}
}

int qp_floor(double altitude) {
    return std::clamp(static_cast<int>(std::upper_bound(std::begin(floors), std::end(floors),
        altitude + 1e-7) - std::begin(floors)), 1, 10);
}
double qp_floor_altitude(int floor) { return floors[std::clamp(floor, 1, 10) - 1]; }
double qp_survival_apm(double altitude) {
    // Attack APM (including cancelled attack), from 2297 unmodded weekly PBs.
    // See docs/QP2_PRESSURE_SAMPLE.md for sampling, limits and reproduction.
    constexpr double heights[]={24,99,224,374,549,748,973,1219,1489,1974,2880,3846,4690};
    constexpr double apm[]={9.4,15.3,19.9,23.6,27.7,35.3,46.8,56.4,66.2,83.4,113.3,143.8,171.5};
    if (!std::isfinite(altitude)) return apm[0];
    if (altitude<=heights[0]) return apm[0];
    for (int i=1;i<13;++i) if (altitude<=heights[i])
        return apm[i-1]+(apm[i]-apm[i-1])*(altitude-heights[i-1])/(heights[i]-heights[i-1]);
    // Sparse data beyond 4.7 km: extrapolate the well-sampled 2.9--3.8 km slope.
    return std::min(1000.0,apm[12]+(altitude-heights[12])*(apm[11]-apm[10])/(heights[11]-heights[10]));
}

int qp_snowball_size(int clearedLines) {
    return 4 + std::clamp(clearedLines / 20, 0, 16);
}

bool qp_hard_mode(const Runtime::QuickPlayConfig& c) {
    return Runtime::enabled(c.expert) || Runtime::reverse(c.noHold) || Runtime::reverse(c.messy) ||
        Runtime::reverse(c.gravity) || Runtime::reverse(c.volatileMode) || Runtime::reverse(c.doubleHole) ||
        Runtime::reverse(c.invisible) || c.allSpin == Runtime::AllSpinMode::Reverse ||
        Runtime::reverse(c.backfire) || Runtime::reverse(c.snakeEyes) || Runtime::reverse(c.snowball) ||
        c.fool == Runtime::FoolMode::Hexomino || c.fool == Runtime::FoolMode::ReverseAprilFools2026;
}

Runtime::Duration qp_garbage_delay(const Runtime::QuickPlayConfig& c, int floor, double cancellation) {
    floor = std::clamp(floor, 1, 10);
    // rEX timing takes precedence over every other Mod and cancellation shortening.
    if (Runtime::reverse(c.expert))
        return Runtime::Duration::from_seconds(2.4 - floor * 0.2);
    double delay = (11 - floor) * 0.5;
    if (Runtime::enabled(c.expert)) delay = 2.4 - floor * 0.2;
    else if (Runtime::reverse(c.messy) || Runtime::reverse(c.volatileMode) || Runtime::reverse(c.doubleHole))
        delay = 2.5;
    else if (qp_hard_mode(c)) delay = std::min(delay, 2.5);
    if (c.continuousCancellation && cancellation >= 25) delay *= 0.5;
    return Runtime::Duration::from_seconds(delay);
}

Runtime::Duration qp_garbage_rise_interval(const Runtime::QuickPlayConfig& c, Runtime::Duration configured) {
    // Expert and its reverse variant remove the garbage rise animation.
    return Runtime::enabled(c.expert) ? Runtime::Duration{} : configured;
}

double qp_messiness(const Runtime::QuickPlayConfig& c, int floor,
                   const Runtime::QuickPlayProgress& p, double elapsed) {
    if (Runtime::reverse(c.expert) && elapsed >= 660) return 1.0;
    double value = floor * (Runtime::enabled(c.expert) ? 0.05 : 0.03);
    if (Runtime::enabled(c.messy)) value += Runtime::reverse(c.messy) ? 1.0 : 0.25;
    if (c.allSpin == Runtime::AllSpinMode::Reverse) value += 0.30;
    if (!Runtime::enabled(c.messy) && !(Runtime::reverse(c.expert) && elapsed >= 360))
        value -= p.receiveProtection * 0.015;
    return std::max(0.0, value);
}

double qp_garbage_favor(const Runtime::QuickPlayConfig& c, int floor) {
    if (Runtime::reverse(c.volatileMode)) return 50;
    return 33 - floor * 3 - (Runtime::enabled(c.expert) ? 33 : 0) - (Runtime::enabled(c.messy) ? 25 : 0);
}

double qp_receive_attack(double attack, double altitude, double attackerAltitude,
                         int floor, const Runtime::QuickPlayConfig& c,
                         const Runtime::QuickPlayProgress& p, double elapsed) {
    const double critical = std::max(std::min(attackerAltitude, 3500.0), std::min(altitude - 1000, 2000.0));
    if (altitude > critical && altitude + critical > 0)
        attack *= 1 + 0.004 * std::pow(altitude - critical, 2) / (altitude + critical);
    const bool vl = Runtime::enabled(c.volatileMode);
    if (c.continuousCancellation) {
        const bool as = Runtime::enabled(c.allSpin);
        const double coefficient = vl && !as ? 0.0003 : as && !vl ? 0.002 : 0.001;
        double cap = floor <= 7 ? floor + 3 : std::numeric_limits<double>::infinity();
        if (elapsed < p.windupUntil) cap = std::min(cap, 6.0);
        if (vl) cap*=2;
        // The client caps the total raised attack, never reducing the base
        // attack and never adding the cap on top of an already large packet.
        attack=std::max(attack,std::min(attack+coefficient*p.cancellation*p.cancellation,cap));
    }
    if (p.receiveProtection > 8) attack *= 1 - 0.05 * (p.receiveProtection - 8);
    const double fatigue = Runtime::reverse(c.expert)
        ? 1 + (elapsed >= 420 ? 0.25 : 0) + (elapsed >= 540 ? 0.25 : 0)
        : 1 + (elapsed >= 540 ? 0.25 : 0) + (elapsed >= 660 ? 0.25 : 0);
    return std::max(0.0, attack * fatigue * p.stallMultiplier *
        (Runtime::reverse(c.volatileMode) ? 3 : vl ? 2 : 1));
}

std::vector<int> qp_windup_segments(int attack, double altitude, bool volatileMode) {
    if (attack < (volatileMode ? 16 : 8)) return attack > 0 ? std::vector<int>{attack} : std::vector<int>{};
    const int extra=std::isfinite(altitude) ? static_cast<int>(std::clamp(std::floor(altitude/500)-7,0.0,1000000.0)) : 0;
    int hypothetical = 16 + extra;
    if (volatileMode) hypothetical *= 2;
    std::vector<int> result;
    for (int i = 0; i < 4 && attack > 0; ++i) {
        const int capacity = hypothetical / 4 + (i >= 4 - hypothetical % 4);
        result.push_back(std::min(attack, capacity));
        attack -= result.back();
    }
    return result;
}

bool qp_queue_windup(Runtime::QuickPlayProgress& p,int attack,double altitude,
                     bool volatileMode,double elapsed) {
    if (attack<(volatileMode ? 16 : 8)) return false;
    const auto segments=qp_windup_segments(attack,altitude,volatileMode);
    const double start=std::max(elapsed,p.windupUntil);
    const double end=start+2+0.5*segments.size();
    p.windupWarnings.push_back({start,end,static_cast<int>(segments.size())});
    for (std::size_t i=0;i<segments.size();++i)
        p.windups.push_back({segments[i],start+1+0.5*i});
    p.windupUntil=end;
    return true;
}

void qp_relieve_cancellation(Runtime::QuickPlayProgress& p, double amount) {
    p.cancellation = std::max(0.0, p.cancellation - amount);
    p.sinceRelief = 0;
    p.piecesSinceRelief = 0;
}

void qp_record_placement(Runtime::QuickPlayProgress& p, const Runtime::QuickPlayConfig& c,
                         int lines, int garbageLines,
                         bool perfectClear, bool spin, int family, int column, int cancelled, bool stackedEdgeI) {
    if (!c.continuousCancellation) return;
    p.cancellation += cancelled + (perfectClear ? 3 : 0);
    if (spin && lines > 0 && (family == 4 || family == 6)) {
        if (std::count(p.szPieces.begin(),p.szPieces.end(),family) >= 5) p.cancellation += 2;
        std::move_backward(p.szPieces.begin(),p.szPieces.end()-1,p.szPieces.end());
        p.szPieces[0] = family;
    }
    if (stackedEdgeI && lines == 0) p.cancellation += 3;
    if (++p.piecesSinceRelief >= 75) { p.cancellation += 5; p.piecesSinceRelief = 0; }
    if (garbageLines > 0) qp_relieve_cancellation(p, p.cancellation);
    if (spin && lines > 0 && family == 0) qp_relieve_cancellation(p, 2);
    if (!spin && lines >= 4) {
        const bool fresh = new_column(p.quadColumns, column);
        qp_relieve_cancellation(p, 3 + (fresh ? 4 : 0));
    }
    if (spin && lines > 0 && (family == 4 || family == 6 || family == 1 || family == 2)) {
        const bool fresh = new_column(family == 4 || family == 6 ? p.szColumns : p.jlColumns, column);
        if (fresh) qp_relieve_cancellation(p, 2);
    }
}

void qp_reward_placement(Runtime::QuickPlayProgress& p, double level, double altitude,
                         int outgoing, int cancelled, int lines, bool spin, int dugLines,
                         const Runtime::QuickPlayConfig& c, double experienceMultiplier) {
    double xp = 0;
    const bool suppressSending = c.allSpin == Runtime::AllSpinMode::Reverse && altitude >= 800 && dugLines < 5;
    const bool creditedCancellation = cancelled > 0 && !Runtime::enabled(c.expert) &&
        !Runtime::reverse(c.volatileMode) && !Runtime::reverse(c.backfire);
    if (outgoing > 0 && !suppressSending) xp += outgoing + 0.05;
    if (creditedCancellation) {
        const int threshold = Runtime::enabled(c.volatileMode) ? 40 : Runtime::enabled(c.allSpin) ? 10 : 25;
        const double cancellation = c.continuousCancellation ? p.cancellation : 0;
        xp += (cancelled + 0.05) * std::max(0.05, 0.5 - std::max(0.0, cancellation - threshold) * 0.005);
    }
    if (lines > 0 && !Runtime::enabled(c.expert))
        xp += std::min(c.allSpin == Runtime::AllSpinMode::Reverse && !spin ? 1 : lines, 2) + 0.05;
    const int floor = qp_floor(altitude);
    if ((outgoing > 0 || creditedCancellation || (lines > 0 && !Runtime::enabled(c.expert))) && floor < 10 &&
        qp_floor_altitude(floor + 1) - altitude - p.pendingAltitude <= 2)
        p.pendingAltitude += 3;
    p.pendingAltitude += outgoing * level * 0.25;
    p.experience += xp * experienceMultiplier;
}

void qp_advance_booster(Runtime::QuickPlayProgress& p, double& level,
                        double& altitude, int floor, const Runtime::QuickPlayConfig& c, double deltaSeconds) {
    if (deltaSeconds <= 0) return;
    level = std::max(1.0, std::floor(level));
    if (p.upgraded && p.experience >= level * 2) { p.upgradeFatigue = 0; p.upgraded = false; }
    if (p.experience >= level * 4) {
        p.experience -= level * 4;
        ++level;
        if (p.experience >= level * 4) level += std::floor(p.experience / (level * 4));
        p.decayProtection = std::max(1, 5 - p.upgradeFatigue);
        p.upgradeFatigue = std::min(4, p.upgradeFatigue + 1);
        p.upgraded = true;
        if (p.experience >= level * 2) { p.upgradeFatigue = 0; p.upgraded = false; }
    }
    const double decaySeconds = std::max(0.0, deltaSeconds - p.decayProtection);
    p.decayProtection = std::max(0.0, p.decayProtection - deltaSeconds);
    if (decaySeconds > 0) {
        const double coefficient = (Runtime::enabled(c.expert) || Runtime::reverse(c.backfire) ? 5 : 3) / 60.0;
        double remaining = decaySeconds;
        while (remaining > 0) {
            const double rate = coefficient * (level * level + level);
            const double timeToEmpty = std::max(0.0, p.experience) / rate;
            if (level <= 1 || remaining <= timeToEmpty) {
                p.experience = std::max(0.0, p.experience - rate * remaining);
                break;
            }
            // Continue draining the rest of this frame at the lower level's rate.
            remaining -= timeToEmpty;
            --level;
            // Recovered XP must stay below the upgrade boundary even when a
            // tiny remaining delta would otherwise round it back to a full bar.
            p.experience = std::nextafter(4 * level, 0.0);
            p.upgraded = false;
        }
    }
    if (!Runtime::reverse(c.expert)) {
        const double distance = floor < 10 ? qp_floor_altitude(floor + 1) - altitude - p.pendingAltitude : 6;
        p.pendingAltitude += level * 0.25 * deltaSeconds * std::clamp((distance - 1) / 5, 0.0, 1.0);
    }
    const double release = std::min(600.0 * deltaSeconds, p.pendingAltitude * (1 - std::pow(0.9, deltaSeconds * 60)));
    p.pendingAltitude -= release;
    altitude += release;
    if (Runtime::reverse(c.expert))
        altitude = std::max(qp_floor_altitude(floor), altitude - (floor * floor + floor + 10) / 20.0 * deltaSeconds);
}

} // namespace Rules
