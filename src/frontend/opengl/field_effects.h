#ifndef TETR_OPENGL_FIELD_EFFECTS_H
#define TETR_OPENGL_FIELD_EFFECTS_H
#include "tetr/presentation/board_view.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace Graphical {
struct FieldOffset {float x=0,y=0;};
struct FieldTransform {float x=0,y=0,rotation=0;};
enum class ParticleKind { Spark, Fragment, Dust };
struct EffectParticle {
    float x=0,y=0,vx=0,vy=0,life=1,size=.1f;
    double born=0;
    int color=0;
    ParticleKind kind=ParticleKind::Spark;
    float angle=0,spin=0,drag=0,gravity=11;
    FieldOffset position(double age) const {
        const float t=float(std::max(0.,age));
        const float travel=drag>0?(1-std::exp(-drag*t))/drag:t;
        return {x+vx*travel,y+vy*travel+gravity*t*t};
    }
};
struct FieldPulse {
    double born=0;
    float energy=1,dropDistance=0;
    int color=0,lines=0;
    bool special=false;
    std::vector<Runtime::CellPosition> cells;
    std::vector<int> rows;
};
struct BoardImpulse {
    double born=0;
    float x=0,y=0,rotation=0,shock=0;
};

// Independent analytic presentation. The two-stage positional decay follows
// TETR.IO's impulse/velocity model: directional pushes, then a smooth return.
// No integration step, game RNG, input polling or gameplay decisions live here.
class FieldEffects {
public:
    static constexpr std::size_t maxParticles=1600,maxPulses=12,maxImpulses=64;
    Runtime::VisualEffectsConfig settings;
    float strength(int channel) const {
        return std::clamp(settings.intensity,0,200)*std::clamp(channel,0,200)/10000.f;
    }
    double time() const {return time_;}
    const std::vector<FieldPulse>& pulses() const {return pulses_;}
    const std::vector<EffectParticle>& particles() const {return particles_;}
    const std::vector<BoardImpulse>& impulses() const {return impulses_;}
    void suspend(double now) {lastNow_=now;wasFrozen_=true;}
    void update(const Presentation::BoardView& b,double now,bool frozen=false,bool replay=false,
        double speed=1,bool finished=false,std::uint64_t roundRevision=0) {
        const double elapsed=b.elapsed.seconds(),dt=std::max(0.,now-lastNow_),rate=replay?std::max(0.,speed):1.;
        const double delta=elapsed-lastElapsed_;
        const int garbage=std::accumulate(b.garbage.begin(),b.garbage.end(),0,
            [](int total,int lines){return total+std::max(0,lines);});
        const bool reset=!known_ || roundRevision!=lastRound_ || now<lastNow_ || b.pieces<lastPieces_ || delta<-.05 ||
            width_!=b.width || height_!=b.bottom || username_!=b.username || (wasFinished_ && !finished) ||
            (replay && !wasFrozen_ && !frozen && std::abs(delta-dt*rate)>.75 && !finished) ||
            (replay && (wasFrozen_ || frozen) && std::abs(delta)>.05);
        settings=b.effects;
        if(reset) {pulses_.clear();particles_.clear();impulses_.clear();time_=0;}
        else if(!frozen && !wasFrozen_) {
            double advance=dt*rate;
            if(!finished && dt>.25 && delta<advance*.5)advance=std::max(0.,delta);
            time_+=advance;
        }
        pulses_.erase(std::remove_if(pulses_.begin(),pulses_.end(),[&](const auto& p){return time_-p.born>1.15;}),pulses_.end());
        particles_.erase(std::remove_if(particles_.begin(),particles_.end(),[&](const auto& p){return time_-p.born>=p.life;}),particles_.end());
        impulses_.erase(std::remove_if(impulses_.begin(),impulses_.end(),[&](const auto& p){return time_-p.born>2;}),impulses_.end());
        if(settings.intensity<=0) {pulses_.clear();particles_.clear();impulses_.clear();}
        else {
            if(!b.spatialEffects) {
                particles_.clear();
                for(auto& p:pulses_){p.cells.clear();p.rows.clear();p.dropDistance=0;}
                for(auto& p:impulses_){p.x=0;p.rotation=0;}
            }
            if(settings.particles<=0)particles_.clear();
            if(settings.bounciness<=0)for(auto& p:impulses_){p.x=p.y=p.rotation=0;}
            if(settings.shake<=0)for(auto& p:impulses_)p.shock=0;
            if(!reset && !frozen) {
                if(b.pieces>lastPieces_)emit(b,replay);
                if(b.spatialEffects && wasSpatial_ && !finished)motion(b);
                if(garbage>lastGarbage_ && !finished)
                    impulse(0,0,0,.7f+std::min(12,garbage-lastGarbage_)*.16f);
            }
        }
        lastPieces_=b.pieces;lastLines_=b.lines;lastElapsed_=elapsed;lastNow_=now;lastRound_=roundRevision;lastGarbage_=garbage;
        width_=b.width;height_=b.bottom;username_=b.username;
        previousActive_=b.active;previousGhost_=b.ghost;lastMotion_=b.motion;
        known_=true;wasFrozen_=frozen;wasFinished_=finished;wasSpatial_=b.spatialEffects;
    }
    FieldOffset shake() const {
        FieldOffset out;const float amount=strength(settings.shake);
        if(amount<=0)return out;
        for(const auto& p:impulses_) {
            const float t=float(time_-p.born);if(t<0 || t>.42f || p.shock<=0)continue;
            const float weight=std::exp(-t*12)*p.shock*amount*.22f;
            out.x+=(std::sin(t*113)+std::sin(t*191)*.4f)*weight;
            out.y+=(std::sin(t*137)*.7f+std::sin(t*83)*.3f)*weight;
        }
        out.x=std::clamp(out.x,-.65f*amount,.65f*amount);out.y=std::clamp(out.y,-.65f*amount,.65f*amount);
        return out;
    }
    FieldTransform transform() const {
        const auto jitter=shake();FieldTransform out{jitter.x,jitter.y,0};
        const float amount=strength(settings.bounciness);
        if(amount<=0)return out;
        const float bounce=std::clamp(settings.bounciness,0,200)/100.f;
        const float retention=.55f+.2f*std::min(bounce,1.8f),angular=.82f+.08f*std::min(bounce,1.5f);
        float x=0,y=0,rotation=0;
        for(const auto& p:impulses_) {
            const float frames=float((time_-p.born)*60);if(frames<0)continue;
            const float response=frames*std::pow(retention,frames);
            x+=p.x*response;y+=p.y*response;rotation+=p.rotation*frames*std::pow(angular,frames);
        }
        out.x+=std::clamp(x*amount,-1.25f,1.25f);
        out.y+=std::clamp(y*amount,-1.6f,2.f);
        out.rotation=std::clamp(rotation*amount,-4.f,4.f);
        return out;
    }
private:
    void impulse(float x,float y,float rotation,float shock=0) {
        if(settings.bounciness<=0)x=y=rotation=0;
        if(settings.shake<=0)shock=0;
        if(x==0 && y==0 && rotation==0 && shock==0)return;
        if(impulses_.size()==maxImpulses)impulses_.erase(impulses_.begin());
        impulses_.push_back({time_,x,y,rotation,shock});
    }
    void motion(const Presentation::BoardView& b) {
        if(b.motion.wallSequence>lastMotion_.wallSequence)
            impulse(.06f*b.motion.wallDirection*float(std::min<std::uint64_t>(3,b.motion.wallSequence-lastMotion_.wallSequence)),0,0);
        if(b.motion.rotationSequence>lastMotion_.rotationSequence)
            impulse(0,0,.30f*b.motion.rotationDirection*float(std::min<std::uint64_t>(2,b.motion.rotationSequence-lastMotion_.rotationSequence)));
        // Remote/legacy replays lack contact counters. React only to visible
        // motion of the same live piece, never to a spawn or a hidden pose.
        if(!previousActive_ || !b.active || b.pieces!=lastPieces_ || previousActive_->type!=b.active->type)return;
        if(!b.motion.wallSequence && !lastMotion_.wallSequence && b.active->pose.position.col!=previousActive_->pose.position.col) {
            const auto definition=Piece::get_definition(b.active->type);
            int left=b.width,right=-1;
            for(const auto& cell:definition.cells(b.active->pose.rotation)) {
                left=std::min(left,b.active->pose.position.col+cell[1]);right=std::max(right,b.active->pose.position.col+cell[1]);
            }
            if(left<=0 || right>=b.width-1)impulse(b.active->pose.position.col<previousActive_->pose.position.col?-.14f:.14f,0,0);
        }
    }
    void emit(const Presentation::BoardView& b,bool replay) {
        FieldPulse pulse;pulse.born=time_;pulse.lines=std::max({0,b.lines-lastLines_,b.placementLines});
        pulse.special=(b.spinPiece.valid() && pulse.lines) || pulse.lines>=4 || b.perfectClear;
        pulse.energy=1+std::min(8,pulse.lines)*.3f+(b.spinPiece.valid() && pulse.lines?.4f:0)+(b.perfectClear?.7f:0);
        std::optional<Presentation::PieceView> locked;std::optional<int> from;
        if(b.placement && b.placement->sequence==b.pieces) {
            locked=b.placement->piece;pulse.rows=b.placement->clearedRows;from=b.placement->dropFromRow;
        } else if(b.pieces==lastPieces_+1 && previousGhost_) {
            locked=previousGhost_;
            if(previousActive_ && previousActive_->type==locked->type)from=previousActive_->pose.position.row;
        }
        if(locked && locked->type.valid()) {
            pulse.color=Piece::metadata(locked->type).colorId;
            if(replay && b.pieces==lastPieces_+1 && !from && previousActive_ && previousActive_->type==locked->type)
                from=previousActive_->pose.position.row;
            if(from)pulse.dropDistance=float(std::max(0,locked->pose.position.row-*from));
            const auto definition=Piece::get_definition(locked->type);
            if(b.spatialEffects)for(const auto& cell:definition.cells(locked->pose.rotation)) {
                const int row=locked->pose.position.row+cell[0],col=locked->pose.position.col+cell[1];
                if(row>=b.firstRow && row<b.bottom && col>=0 && col<b.width)pulse.cells.push_back({row,col});
            }
        }
        if(!b.spatialEffects){pulse.rows.clear();pulse.dropDistance=0;}
        else if(pulse.rows.empty() && pulse.lines && !pulse.cells.empty()) {
            const int bottom=std::max_element(pulse.cells.begin(),pulse.cells.end(),[](const auto& a,const auto& c){return a.row<c.row;})->row;
            for(int n=0;n<std::min(pulse.lines,b.bottom);++n)pulse.rows.push_back(std::max(0,bottom-n));
        }
        pulse.rows.erase(std::remove_if(pulse.rows.begin(),pulse.rows.end(),[&](int row){return row<0 || row>=b.bottom;}),pulse.rows.end());
        // Downward momentum is proportional to the actual clear, as in the
        // reference holder. Heavy pieces add weight without altering physics.
        const float weight=std::min(1.8f,std::sqrt(std::max(4.f,float(pulse.cells.size()))/4));
        impulse(0,(.16f+.235f*std::min(6,pulse.lines))*weight,0,pulse.special?.65f:0);
        // Older replays and remote boards expose the confirmed Spin at lock,
        // rather than a live rotation contact. Trigger that known event once.
        if(b.spinPiece.valid() && !b.motion.rotationSequence)
            impulse(0,0,.30f*(b.motion.rotationDirection?b.motion.rotationDirection:1));
        const float amount=strength(settings.particles);
        std::uint32_t random=std::uint32_t(b.pieces)*747796405u+std::uint32_t(b.lines)*2891336453u+1u;
        const auto sample=[&]() {random^=random<<13;random^=random>>17;random^=random<<5;return (random&0xffffff)/16777216.f;};
        const int count=std::min(640,int((pulse.cells.size()*18+pulse.rows.size()*std::min(40,b.width)*7)*amount));
        for(int i=0;i<count;++i) {
            const bool clear=!pulse.rows.empty() && (pulse.cells.empty() || i%4!=0);
            EffectParticle p;p.born=time_;p.color=i%5==0?-1:pulse.color;
            p.kind=i%7==0?ParticleKind::Fragment:i%3==0?ParticleKind::Dust:ParticleKind::Spark;
            if(clear){p.x=sample()*b.width;p.y=pulse.rows[std::size_t(sample()*pulse.rows.size())]+sample();}
            else if(!pulse.cells.empty()) {
                const auto& cell=pulse.cells[std::size_t(sample()*pulse.cells.size())];p.x=cell.col+.1f+.8f*sample();p.y=cell.row+.75f;
            } else break;
            const float side=p.x<b.width/2.f?-1.f:1.f;
            p.vx=clear?side*(3+sample()*15):(sample()-.5f)*17;
            p.vy=-2-sample()*(clear?14.f:8.f);p.life=.45f+sample()*.85f;
            p.angle=sample()*6.2831853f;p.spin=(sample()-.5f)*16;
            p.size=p.kind==ParticleKind::Fragment?.09f+.14f*sample():.025f+.055f*sample();
            p.drag=p.kind==ParticleKind::Dust?2.f:.7f;p.gravity=p.kind==ParticleKind::Dust?-1.f:9.f;
            particles_.push_back(p);
        }
        if(particles_.size()>maxParticles)particles_.erase(particles_.begin(),particles_.end()-maxParticles);
        if(pulses_.size()==maxPulses)pulses_.erase(pulses_.begin());
        pulses_.push_back(std::move(pulse));
    }
    std::vector<FieldPulse> pulses_;
    std::vector<EffectParticle> particles_;
    std::vector<BoardImpulse> impulses_;
    std::optional<Presentation::PieceView> previousActive_,previousGhost_;
    Runtime::BoardMotionFeedback lastMotion_;
    double time_=0,lastNow_=0,lastElapsed_=0;
    int lastPieces_=0,lastLines_=0,lastGarbage_=0,width_=0,height_=0;
    bool known_=false,wasFrozen_=false,wasFinished_=false,wasSpatial_=true;
    std::uint64_t lastRound_=0;
    std::string username_;
};
}
#endif
