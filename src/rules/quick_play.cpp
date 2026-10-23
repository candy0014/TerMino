#include "tetr/rules/quick_play.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Rules {
namespace {
constexpr double floors[] = {0, 50, 150, 300, 450, 650, 850, 1100, 1350, 1650};
constexpr int snowballStartSize=4,snowballMaxSteps=16,snowballStepLines=20;

bool new_column(std::array<int, 2>& history, int column,bool legacy) {
    if (column == history[0]) return false;
    if (column == history[1]) {
        if(!legacy)std::swap(history[0],history[1]);
        return false;
    }
    const bool full=history[0]>=0 && history[1]>=0;
    history[1] = history[0];
    history[0] = column;
    return legacy || full;
}
}

int qp_floor(double altitude) {
    return std::clamp(static_cast<int>(std::upper_bound(std::begin(floors), std::end(floors),
        altitude + 1e-7) - std::begin(floors)), 1, 10);
}
double qp_floor_altitude(int floor) { return floors[std::clamp(floor, 1, 10) - 1]; }
QuickPlayPressure qp_pressure(double altitude,double ownApm) {
    const double h=std::log1p(std::clamp(std::isfinite(altitude)?altitude:0.,0.,6000.)/500);
    const double a=std::log1p(std::clamp(std::isfinite(ownApm)?ownApm:0.,0.,500.)/40);
    const double x[]={1,h,a,h*a,h*h,a*a};
    // 8,776 unmodded runs, 2026w41/w42; integrated over floor split times.
    constexpr double mean[]={2.084314870996432,3.657281523249494,0.5492664962205321,1.393225095971763,-4.706876036487392,-0.3107438265361438};
    constexpr double variance[]={-12.98628097000187,11.98876510849465,5.71835644738461,-5.057042130752374,-4.178153015198679,0.9670166442983178};
    // Saturate at the fitted peak instead of extrapolating a falling tail.
    const double hm=std::min(h,std::max(0.,-(mean[1]+mean[3]*a)/(2*mean[4])));
    const double xm[]={1,hm,a,hm*a,hm*hm,a*a};
    double m=0,v=0;for(int i=0;i<6;++i){m+=xm[i]*mean[i];v+=x[i]*variance[i];}
    // Unsupported combinations/tails must stay finite and playable. These
    // safety bounds are simulation choices, not measurements of the server.
    return {std::exp(std::clamp(m,0.,std::log(180.))),std::exp(std::clamp(v,-9.,0.))};
}
double qp_survival_apm(double altitude) {return qp_pressure(altitude,40).mean;}

int qp_snowball_size(int clearedLines) {
    return snowballStartSize + std::clamp(clearedLines / snowballStepLines, 0, snowballMaxSteps);
}

int qp_snowball_lines_until_growth(int clearedLines) {
    const int lines=std::max(0,clearedLines);
    if(lines/snowballStepLines>=snowballMaxSteps)return 0;
    return snowballStepLines-lines%snowballStepLines;
}

bool qp_hard_mode(const Runtime::QuickPlayConfig& c) {
    return Runtime::enabled(c.expert) || Runtime::reverse(c.noHold) || Runtime::reverse(c.messy) ||
        Runtime::reverse(c.gravity) || Runtime::reverse(c.volatileMode) || Runtime::reverse(c.doubleHole) ||
        Runtime::reverse(c.invisible) || c.allSpin == Runtime::AllSpinMode::Reverse ||
        Runtime::reverse(c.backfire) || Runtime::reverse(c.snakeEyes) || Runtime::reverse(c.snowball) ||
        c.fool == Runtime::FoolMode::Hexomino || c.fool == Runtime::FoolMode::ReverseAprilFools2026;
}

bool qp_gravity_increases(const Runtime::QuickPlayConfig& c) {
    return c.legacyRules || (!Runtime::reverse(c.doubleHole) && c.allSpin!=Runtime::AllSpinMode::Reverse);
}

int qp_fatigue_garbage_due(const Runtime::QuickPlayConfig& c,double elapsed) {
    constexpr int offsets[]={0,30,60,110,160,220,280,350,420,500,580,650};
    constexpr int normal[]={2,3,5},expert[]={3,5,12};
    int due=0;
    for(int wave=0;wave<3;++wave)for(int row=0;row<(Runtime::reverse(c.expert)?expert[wave]:normal[wave]);++row)
        due+=elapsed+1e-7>=480+120*wave+(c.legacyRules?double(row):offsets[row]/60.0);
    return due;
}

bool qp_garbage_concentrates(const Runtime::QuickPlayConfig& c,int floor,
    const Runtime::QuickPlayProgress& p,double elapsed) {
    if(Runtime::reverse(c.volatileMode))return true;
    const double messiness=qp_messiness(c,floor,c.legacyRules?p:Runtime::QuickPlayProgress{},elapsed);
    return floor<=5 && messiness*(c.legacyRules?2.5:1)<=.15;
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
    if (c.continuousCancellation && cancellation >= 25 &&
        (c.legacyRules || !Runtime::enabled(c.volatileMode))) delay *= 0.5;
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
    const double receiveMultiplier=c.legacyRules ? fatigue*p.stallMultiplier : fatigue+p.stallMultiplier-1;
    return std::max(0.0, attack * receiveMultiplier *
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
    if(c.legacyRules) {
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
            const bool fresh = new_column(p.quadColumns, column,true);
            qp_relieve_cancellation(p, 3 + (fresh ? 4 : 0));
        }
        if (spin && lines > 0 && (family == 4 || family == 6 || family == 1 || family == 2)) {
            const bool fresh = new_column(family == 4 || family == 6 ? p.szColumns : p.jlColumns, column,true);
            if (fresh) qp_relieve_cancellation(p, 2);
        }
        return;
    }
    if(!c.continuousCancellation)return;
    if(p.piecesSinceRelief<76)++p.piecesSinceRelief;
    if(p.piecesSinceRelief==75)p.cancellation+=5;
    if(garbageLines>0)qp_relieve_cancellation(p,p.cancellation);
    if(spin && lines>0 && family==0)qp_relieve_cancellation(p,2);
    if(spin && lines>0 && (family==4 || family==6 || family==1 || family==2)) {
        if(new_column(family==4 || family==6?p.szColumns:p.jlColumns,column,false))qp_relieve_cancellation(p,2);
    }
    if(stackedEdgeI && !lines)p.cancellation+=3;
    if(spin && lines>0 && (family==4 || family==6)) {
        if(std::count(p.szPieces.begin(),p.szPieces.end(),family)>=5)p.cancellation+=2;
        std::move_backward(p.szPieces.begin(),p.szPieces.end()-1,p.szPieces.end());p.szPieces[0]=family;
    }
    if(lines==4 || lines==5) {
        const bool fresh=new_column(p.quadColumns,column,false);
        qp_relieve_cancellation(p,3+(fresh?4:0));
    }
    // Clear/terrain relief precedes PC and this placement's cancellation.
    p.cancellation+=cancelled+(perfectClear?3:0);
}

void qp_reward_placement(Runtime::QuickPlayProgress& p, double level, double altitude,
                         int outgoing, int cancelled, int lines, bool spin, int dugLines,
                         const Runtime::QuickPlayConfig& c, double experienceMultiplier) {
    if(c.legacyRules) {
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
        return;
    }
    double xp=0;
    const auto award=[&](double height,double experience) {
        const int floor=qp_floor(altitude);
        const double gap=floor<10 ? qp_floor_altitude(floor+1)-altitude-p.pendingAltitude-height : -1;
        if(gap>=0 && gap<=2)height+=3;
        p.pendingAltitude+=height;xp+=experience;
    };
    // Clear XP is awarded before cancellation and sending in the actual lock
    // path. Crossing checks include THIS action's altitude and reject overshoot.
    if(lines>0 && !Runtime::enabled(c.expert))
        award(0,std::min(c.allSpin==Runtime::AllSpinMode::Reverse && !spin?1:lines,2)+.05);
    if(cancelled>0) {
        double experience=0;
        if(!Runtime::enabled(c.expert) && !Runtime::reverse(c.volatileMode) && !Runtime::reverse(c.backfire)) {
            const int threshold=Runtime::enabled(c.volatileMode)?40:Runtime::enabled(c.allSpin)?10:25;
            const double cancellation=c.continuousCancellation?p.cancellation:0;
            experience=(cancelled+.05)*std::max(.05,.5-std::max(0.,cancellation-threshold)*.005);
        }
        award(0,experience);
    }
    if(outgoing>0)award(outgoing*level*.25,outgoing+.05);
    // The reviewed client gates GiveClimbPts for every AwardLines call here,
    // including clear/cancel XP, while altitude bonuses still apply.
    if(c.allSpin==Runtime::AllSpinMode::Reverse && altitude>=800 && dugLines<5)xp=0;
    p.experience+=xp*experienceMultiplier;
}

void qp_advance_booster(Runtime::QuickPlayProgress& p, double& level,
                        double& altitude, int floor, const Runtime::QuickPlayConfig& c, double deltaSeconds) {
    if (deltaSeconds <= 0) return;
    level = std::max(1.0, std::floor(level));
    const auto drain=[&](double seconds) {
        const double decaySeconds = std::max(0.0, seconds - p.decayProtection);
        p.decayProtection = std::max(0.0, p.decayProtection - seconds);
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
    };
    if(!c.legacyRules)drain(deltaSeconds);
    double fatigueLevel=level;
    if (c.legacyRules && p.upgraded && p.experience >= level * 2) { p.upgradeFatigue = 0; p.upgraded = false; }
    if (p.experience >= level * 4) {
        p.experience -= level * 4;
        ++level;fatigueLevel=level;
        if (p.experience >= level * 4) level += std::floor(p.experience / (level * 4));
        p.decayProtection = std::max(1, 5 - p.upgradeFatigue);
        p.upgradeFatigue = c.legacyRules ? std::min(4, p.upgradeFatigue + 1) :
            p.upgradeFatigue + (p.upgradeFatigue < std::numeric_limits<int>::max());
        p.upgraded = true;
        if (c.legacyRules && p.experience >= level * 2) { p.upgradeFatigue = 0; p.upgraded = false; }
    }
    if(c.legacyRules)drain(deltaSeconds);
    else if(p.upgraded && p.experience>=2*(fatigueLevel-1)) {p.upgradeFatigue=0;p.upgraded=false;}
    if (!Runtime::reverse(c.expert)) {
        const double distance = floor < 10 ? qp_floor_altitude(floor + 1) - altitude - (c.legacyRules?p.pendingAltitude:0) : 6;
        const double natural=(c.legacyRules?level:fatigueLevel)*.25*deltaSeconds*std::clamp((distance-1)/5,0.,1.);
        if(c.legacyRules)p.pendingAltitude+=natural;else altitude+=natural;
    }
    if(!c.legacyRules && Runtime::reverse(c.expert))
        altitude=std::max(qp_floor_altitude(floor),altitude-(floor*floor+floor+10)/20.0*deltaSeconds);
    const double release = !c.legacyRules && p.pendingAltitude<=.05 ? p.pendingAltitude : std::min(600.0 * deltaSeconds, p.pendingAltitude * (1 - std::pow(0.9, deltaSeconds * 60)));
    p.pendingAltitude -= release;
    altitude += release;
    if (c.legacyRules && Runtime::reverse(c.expert))
        altitude = std::max(qp_floor_altitude(floor), altitude - (floor * floor + floor + 10) / 20.0 * deltaSeconds);
}

} // namespace Rules
