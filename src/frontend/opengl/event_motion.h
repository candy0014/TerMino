#ifndef TETR_OPENGL_EVENT_MOTION_H
#define TETR_OPENGL_EVENT_MOTION_H
#include "tetr/presentation/board_view.h"
#include "tetr/rules/spike.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Graphical {
enum class EventKind { Action, Combo, Spike, AllClear, Count };
struct TextMotion {
    float opacity=0,spread=0,scale=1,dy=0,echoOpacity=0,echoScale=1;
};
// UI-only time and retained event values. Never writes a gameplay snapshot.
class BoardEvents {
public:
    struct Action {Piece::Type spin;bool mini=false;int lines=0;} action;
    int combo=0,spike=0;
    bool perfectClear=false,halfClear=false;

    void update(const Presentation::BoardView& b,double now,bool frozen=false,
        bool replay=false,double speed=1,bool finished=false,std::uint64_t roundRevision=0) {
        const double elapsed=b.elapsed.seconds(),rate=replay?std::max(0.,speed):1.;
        const double dt=std::max(0.,now-lastNow_),gameDelta=elapsed-lastElapsed_;
        const bool reset=!known_ || roundRevision!=lastRound_ || now<lastNow_ || b.pieces<lastPieces_ || gameDelta<-.05 ||
            (wasFinished_ && !finished) ||
            b.username!=username_ || (replay && !wasFrozen_ && !frozen &&
                std::abs(gameDelta-dt*rate)>.75 && !finished) ||
            (replay && (wasFrozen_ || frozen) && std::abs(gameDelta)>.05);
        if(reset) {
            for(auto& track:tracks_)track={};
            action={};combo=spike=0;perfectClear=halfClear=false;
        } else if(!frozen && !wasFrozen_) {
            // A full settings/results page may have suspended the game while
            // no board was drawn. Do not charge that hidden pause to effects.
            double advance=dt*rate;
            if(!finished && dt>.25 && gameDelta<advance*.5)advance=std::max(0.,gameDelta);
            for(auto& track:tracks_)if(track.active)track.age+=advance;
        }
        const bool placed=reset || b.pieces!=lastPieces_;
        const bool changedAction=b.spinPiece!=lastSpin_ || b.mini!=lastMini_ || b.clearLines!=lastLines_;
        if(b.pieces>0 && (placed || changedAction) && (b.spinPiece.valid() || b.clearLines)) {
            action={b.spinPiece,b.mini,b.clearLines};start(EventKind::Action);
        }
        if(b.pieces>0 && b.combo>1 && (placed || b.combo!=lastCombo_)) {combo=b.combo;start(EventKind::Combo);}
        if(b.pieces>0 && b.spike>=Rules::kMinimumVisibleSpike && (reset || b.spike!=lastSpike_ || b.attack>lastAttack_)) {
            spike=b.spike;start(EventKind::Spike);
        }
        if(b.pieces>0 && (b.perfectClear || b.halfClear) && (placed || b.perfectClear!=lastPerfect_ || b.halfClear!=lastHalf_)) {
            perfectClear=b.perfectClear;halfClear=b.halfClear;start(EventKind::AllClear);
        }
        known_=true;lastNow_=now;lastElapsed_=elapsed;wasFrozen_=frozen;wasFinished_=finished;
        lastRound_=roundRevision;
        lastPieces_=b.pieces;lastSpin_=b.spinPiece;lastMini_=b.mini;lastLines_=b.clearLines;
        lastCombo_=b.combo;lastSpike_=b.spike;lastAttack_=b.attack;
        lastPerfect_=b.perfectClear;lastHalf_=b.halfClear;username_=b.username;
    }
    void suspend(double now) {lastNow_=now;wasFrozen_=true;}
    TextMotion motion(EventKind kind) const {
        const auto& track=tracks_[static_cast<std::size_t>(kind)];
        if(!track.active)return {};
        const float duration=kind==EventKind::Spike?1.5f:kind==EventKind::AllClear?2.f:6.f;
        const float t=static_cast<float>(track.age)/duration;
        if(t>=1)return {};
        const float fadeStart=kind==EventKind::AllClear?.25f:.5f;
        const float fade=std::clamp((t-fadeStart)/(1-fadeStart),0.f,1.f);
        TextMotion out;
        out.opacity=(1-.12f*t)*std::pow(1-fade,1.5f);
        if(kind==EventKind::Action)out.spread=.65f*(1-std::pow(1-t,2.f));
        if(kind==EventKind::Combo || kind==EventKind::Spike) {
            const float bounceTime=kind==EventKind::Spike?.18f:.55f;
            out.dy=track.age<bounceTime?-8*std::sin(float(track.age)/bounceTime*3.14159265f):0;
            out.spread=.18f*fade; // A small final release after the count's bounce.
        }
        if(kind==EventKind::AllClear) {
            const float pop=std::clamp(float(track.age)/.35f,0.f,1.f);
            out.scale=1+.10f*std::sin(pop*3.14159265f)-.14f*fade;
            out.spread=.16f*fade;
            out.echoOpacity=.18f*std::pow(1-t,2.f)*std::min(1.f,float(track.age)/.12f);
            out.echoScale=1+1.8f*(1-std::pow(1-t,2.f));
        }
        return out;
    }
private:
    struct Track {double age=0;bool active=false;};
    void start(EventKind kind) {tracks_[static_cast<std::size_t>(kind)]={0,true};}
    std::array<Track,static_cast<std::size_t>(EventKind::Count)> tracks_{};
    bool known_=false,wasFrozen_=false,wasFinished_=false,lastMini_=false,lastPerfect_=false,lastHalf_=false;
    int lastPieces_=0,lastLines_=0,lastCombo_=0,lastSpike_=0,lastAttack_=0;
    double lastNow_=0,lastElapsed_=0;
    std::uint64_t lastRound_=0;
    Piece::Type lastSpin_;
    std::string username_;
};
}
#endif
