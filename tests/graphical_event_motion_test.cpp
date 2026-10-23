#include "../src/frontend/opengl/event_motion.h"
#include "../src/frontend/opengl/field_effects.h"
#include <cassert>
#include <cmath>

namespace TetrTests::graphical_event_motion_test {
using Graphical::BoardEvents;
using Graphical::EventKind;
namespace {
Presentation::BoardView event() {
    Presentation::BoardView b;b.pieces=10;b.elapsed=Runtime::Duration::from_seconds(10);
    b.spinPiece=Piece::Type::from_index(5);b.clearLines=2;b.combo=4;b.spike=12;b.attack=20;
    b.perfectClear=true;return b;
}
void tick(BoardEvents& state,Presentation::BoardView& b,double time) {
    b.elapsed=Runtime::Duration::from_seconds(10+time);state.update(b,time);
}
bool same(float a,float b) {return std::abs(a-b)<.0001f;}
void fades_and_motion() {
    auto b=event();BoardEvents state;tick(state,b,0);
    assert(state.motion(EventKind::Action).opacity==1);
    tick(state,b,.1);assert(state.motion(EventKind::Combo).dy<0);
    assert(state.motion(EventKind::Spike).dy<0 && state.motion(EventKind::AllClear).echoOpacity>0);
    tick(state,b,1.01);assert(state.motion(EventKind::Spike).opacity<state.motion(EventKind::Combo).opacity);
    tick(state,b,1.2);
    const auto clear=state.motion(EventKind::AllClear);
    assert(clear.opacity>0 && clear.opacity<.4f && clear.echoScale>2);
    tick(state,b,2.01);assert(state.motion(EventKind::AllClear).opacity==0);
    tick(state,b,3.5);const auto mid=state.motion(EventKind::Action);
    assert(mid.opacity>0 && mid.opacity<1 && mid.spread>.4f);
    assert(state.motion(EventKind::Spike).opacity==0);
    tick(state,b,6.01);assert(state.motion(EventKind::Action).opacity==0 && state.motion(EventKind::Combo).opacity==0);
    assert(state.motion(EventKind::Spike).opacity==0);
    // More identical snapshots never revive an expired event.
    tick(state,b,20);assert(state.motion(EventKind::Action).opacity==0);
}
void retained_and_retriggered() {
    auto b=event();BoardEvents state;tick(state,b,0);tick(state,b,1);
    ++b.pieces;tick(state,b,1.1);assert(state.motion(EventKind::Action).opacity==1); // identical consecutive TSD
    assert(state.motion(EventKind::AllClear).opacity==1 && state.motion(EventKind::Combo).opacity==1);
    // A non-clearing placement leaves the old text to finish its animation.
    ++b.pieces;b.spinPiece=Piece::Type::none();b.clearLines=0;b.combo=0;b.perfectClear=false;b.spike=0;
    tick(state,b,1.2);assert(state.action.lines==2 && state.action.spin.valid() && state.combo==4);
    assert(state.motion(EventKind::Action).opacity<1);
    // A new ordinary clear replaces the action as a pair: no stale T-SPIN over SINGLE.
    ++b.pieces;b.clearLines=1;tick(state,b,1.3);
    assert(state.action.lines==1 && !state.action.spin.valid() && state.motion(EventKind::Action).opacity==1);
    b.halfClear=true;tick(state,b,1.4);assert(state.halfClear && !state.perfectClear);
}
void spike_does_not_restart_on_unrelated_placements() {
    auto b=event();BoardEvents state;tick(state,b,0);tick(state,b,.4);
    ++b.pieces;tick(state,b,.5);const float faded=state.motion(EventKind::Spike).opacity;
    assert(faded>0 && faded<1);
    b.spike=15;++b.attack;tick(state,b,.6);assert(state.motion(EventKind::Spike).opacity==1 && state.spike==15);
    tick(state,b,.9);++b.attack;tick(state,b,1);assert(state.motion(EventKind::Spike).opacity==1);
}
void spike_expires_after_one_and_a_half_seconds() {
    auto b=event();BoardEvents state;tick(state,b,0);
    tick(state,b,.74);assert(state.motion(EventKind::Spike).opacity>.8f);
    tick(state,b,1.49);assert(state.motion(EventKind::Spike).opacity>0);
    tick(state,b,1.5);assert(state.motion(EventKind::Spike).opacity==0);
    assert(state.motion(EventKind::Combo).opacity>0 && state.motion(EventKind::Action).opacity>0);
    ++b.attack;b.spike=20;tick(state,b,1.6);assert(state.motion(EventKind::Spike).opacity==1);
    tick(state,b,3.11);assert(state.motion(EventKind::Spike).opacity==0);
    float opacity=-1;
    for(int hz:{30,60,144}) {
        auto frame=event();BoardEvents animated;tick(animated,frame,0);
        for(int i=1;i<=hz;++i)tick(animated,frame,double(i)/hz);
        if(opacity>=0)assert(same(opacity,animated.motion(EventKind::Spike).opacity));
        opacity=animated.motion(EventKind::Spike).opacity;
    }
}
void pause_menu_finish_and_seek() {
    auto b=event();BoardEvents state;tick(state,b,0);tick(state,b,.4);
    const auto paused=state.motion(EventKind::Action);
    state.update(b,5,true);state.update(b,10,true);state.update(b,10.1);
    assert(same(state.motion(EventKind::Action).spread,paused.spread));
    b.elapsed=Runtime::Duration::from_seconds(10.5);state.update(b,10.2);
    assert(state.motion(EventKind::Action).spread>paused.spread);
    const auto menu=state.motion(EventKind::Action);state.suspend(100);state.update(b,100.1);
    assert(same(state.motion(EventKind::Action).spread,menu.spread));
    // The completed gameplay clock stops; presentation still completes its fade.
    state.update(b,107,false,false,1,true);assert(state.motion(EventKind::AllClear).opacity==0);
    b.pieces=0;b.elapsed=Runtime::Duration{};state.update(b,108);
    assert(state.motion(EventKind::Action).opacity==0); // no placement feedback before the first lock
    b.pieces=1;b.elapsed=Runtime::Duration::from_seconds(.2);state.update(b,108.1,false,true,2);
    b.elapsed=Runtime::Duration::from_seconds(2);state.update(b,109,false,true,2);
    const auto fast=state.motion(EventKind::Action);assert(fast.spread>0);
    b.elapsed=Runtime::Duration::from_seconds(20);state.update(b,109.1,false,true,2);
    assert(state.motion(EventKind::Action).spread==0); // forward seek
    state.update(b,110,true,true,2);
    b.elapsed=Runtime::Duration::from_seconds(25);state.update(b,110.1,true,true,2);
    assert(state.motion(EventKind::Action).spread==0); // seek while paused
    b.elapsed=Runtime::Duration::from_seconds(1);state.update(b,110.2,false,true,2);
    assert(state.motion(EventKind::Action).spread==0); // backward seek
}
void new_round_drops_previous_events() {
    auto b=event();BoardEvents state;tick(state,b,0);
    b.pieces=0;b.elapsed={};state.update(b,.01);
    for(const auto kind:{EventKind::Action,EventKind::Combo,EventKind::Spike,EventKind::AllClear})
        assert(state.motion(kind).opacity==0);
    assert(!state.action.spin.valid() && !state.action.lines && !state.combo && !state.spike && !state.perfectClear && !state.halfClear);
    // The first rendered frame of a fast new game can already exceed the old
    // game's piece count and time. Finishing still establishes a new boundary.
    b=event();state.update(b,1,false,false,1,true);
    b.pieces=20;b.elapsed=Runtime::Duration::from_seconds(20);
    b.spinPiece=Piece::Type::none();b.clearLines=b.combo=b.spike=0;b.perfectClear=false;
    state.update(b,1.01);
    for(const auto kind:{EventKind::Action,EventKind::Combo,EventKind::Spike,EventKind::AllClear})
        assert(state.motion(kind).opacity==0);
    ++b.pieces;b.clearLines=1;state.update(b,1.02);
    assert(state.motion(EventKind::Action).opacity==1 && state.action.lines==1);
    // A round revision also works when neither the previous finish nor the new
    // countdown was rendered, and the new game already has more pieces/time.
    b=event();state.update(b,2,false,false,1,false,1);
    b.pieces=20;b.elapsed=Runtime::Duration::from_seconds(20);
    b.spinPiece=Piece::Type::none();b.clearLines=b.combo=b.spike=0;b.perfectClear=false;
    state.update(b,2.01,false,false,1,false,2);
    for(const auto kind:{EventKind::Action,EventKind::Combo,EventKind::Spike,EventKind::AllClear})
        assert(state.motion(kind).opacity==0);
}
void cadence_and_boards() {
    float opacity=0,spread=0;
    for(int hz:{30,60,144}) {
        auto b=event();BoardEvents state;tick(state,b,0);
        for(int i=1;i<=hz*4;++i)tick(state,b,double(i)/hz);
        const auto result=state.motion(EventKind::Action);
        if(opacity){assert(same(opacity,result.opacity));assert(same(spread,result.spread));}
        opacity=result.opacity;spread=result.spread;
    }
    auto b=event();BoardEvents one,two;tick(one,b,0);tick(two,b,0);tick(one,b,5);
    assert(two.motion(EventKind::Action).opacity==1 && one.motion(EventKind::Action).opacity<1);
}
}
void field_feedback_lifecycle() {
    auto b=event();b.width=10;b.bottom=20;b.username="one";b.pieces=0;b.lines=0;b.elapsed={};
    Graphical::FieldEffects effects;effects.update(b,0);
    const auto lock=[&](int sequence) {
        b.pieces=sequence;b.lines+=4;b.elapsed=Runtime::Duration::from_seconds(sequence*.1);
        b.placement=Presentation::PlacementView{sequence,{Piece::Type::from_index(0),{{17,4},Runtime::Rotation(1)}},{16,17,18,19},-2};
    };
    lock(1);effects.update(b,.1);
    assert(effects.pulses().size()==1 && !effects.particles().empty());
    assert(effects.pulses()[0].rows.size()==4 && effects.pulses()[0].dropDistance==19);
    b.elapsed=Runtime::Duration::from_seconds(.15);effects.update(b,.15);
    assert(std::abs(effects.shake().x)+std::abs(effects.shake().y)>0);
    const auto count=effects.particles().size();const auto age=effects.time();
    effects.update(b,5,true);effects.update(b,10,true);effects.update(b,10.01);
    assert(effects.time()==age && effects.particles().size()==count);
    effects.suspend(20);effects.update(b,21);assert(effects.time()==age);
    b.effects.intensity=0;effects.update(b,21.01);
    assert(effects.pulses().empty() && effects.particles().empty() && effects.shake().x==0 && effects.shake().y==0);
    b.effects.intensity=100;effects.update(b,21.02);assert(effects.pulses().empty());
    lock(2);effects.update(b,21.03);assert(!effects.particles().empty());
    b.spatialEffects=false;effects.update(b,21.04);
    assert(effects.particles().empty() && effects.pulses().back().cells.empty() && effects.pulses().back().rows.empty());
    b.spatialEffects=true;b.effects.shake=0;b.effects.particles=0;lock(3);effects.update(b,21.05);
    assert(effects.particles().empty() && effects.shake().x==0 && effects.shake().y==0);
    b.effects={200,200,200,200,200,200,200};
    for(int i=4;i<200;++i){lock(i);effects.update(b,21.05+i*.001);}
    assert(effects.particles().size()<=Graphical::FieldEffects::maxParticles && effects.pulses().size()<=Graphical::FieldEffects::maxPulses);
    effects.update(b,22,false,false,1,false,1);assert(effects.pulses().empty() && effects.particles().empty());
    lock(201);effects.update(b,22.01,false,false,1,false,1);assert(!effects.pulses().empty());
    b.pieces=0;b.elapsed={};effects.update(b,23,false,false,1,false,1);assert(effects.pulses().empty());
}
void field_feedback_cadence_and_replay() {
    Graphical::FieldTransform previous;float previousX=0,previousY=0;bool first=true;
    for(int hz:{30,60,144}) {
        Presentation::BoardView b;b.width=10;b.bottom=20;
        Graphical::FieldEffects effects;effects.update(b,0);
        b.pieces=1;b.placement=Presentation::PlacementView{1,{Piece::Type::from_index(5),{{18,4},Runtime::Rotation{}}},{19},0};
        b.motion.rotationSequence=1;b.motion.rotationDirection=1;effects.update(b,0);
        for(int n=1;n<=hz/6;++n){b.elapsed=Runtime::Duration::from_seconds(double(n)/hz);effects.update(b,double(n)/hz);}
        const auto p=effects.particles().front();const float age=float(effects.time()-p.born);
        const auto position=p.position(age);const float x=position.x,y=position.y;
        if(!first){assert(same(previous.x,effects.transform().x) && same(previous.y,effects.transform().y) && same(previous.rotation,effects.transform().rotation));assert(same(x,previousX) && same(y,previousY));}
        previous=effects.transform();previousX=x;previousY=y;first=false;
    }
    Presentation::BoardView b;b.width=10;b.bottom=20;Graphical::FieldEffects effects;effects.update(b,0,false,true,2);
    b.pieces=1;b.placement=Presentation::PlacementView{1,{Piece::Type::from_index(5),{{18,4},Runtime::Rotation{}}},{19},0};
    effects.update(b,0,false,true,2);
    b.elapsed=Runtime::Duration::from_seconds(.2);effects.update(b,.1,false,true,2);assert(std::abs(effects.time()-.2)<.001);
    b.elapsed=Runtime::Duration::from_seconds(10);effects.update(b,.2,false,true,2);assert(effects.pulses().empty() && effects.particles().empty());
    b.elapsed=Runtime::Duration::from_seconds(2);effects.update(b,.3,false,true,2);assert(effects.pulses().empty());
    // Independent local/remote boards never share particles or impulses.
    Graphical::FieldEffects other;other.update(b,0);assert(other.particles().empty() && other.shake().x==0);
}
void board_elasticity_contacts_and_strength() {
    Presentation::BoardView b;b.width=10;b.bottom=20;b.effects.shake=0;
    Graphical::FieldEffects state;state.update(b,0);
    b.motion={1,1,-1,1};state.update(b,.01);
    state.update(b,.06);auto left=state.transform();
    assert(left.x<-.05f && left.rotation>.3f); // visible wall push and directional tilt
    b.elapsed=Runtime::Duration::from_seconds(1);state.update(b,1);assert(std::abs(state.transform().x)<.01f && std::abs(state.transform().rotation)<.05f);
    b.motion.wallSequence++;b.motion.wallDirection=1;state.update(b,1.01);state.update(b,1.06);
    assert(state.transform().x>.05f);
    // Repeated real wall contacts work even though the active pose does not move.
    b.motion.wallSequence+=8;state.update(b,1.07);state.update(b,1.1);
    assert(state.transform().x>.2f && state.impulses().size()<=Graphical::FieldEffects::maxImpulses);
    b.effects.bounciness=0;state.update(b,1.11);assert(state.transform().x==0 && state.transform().rotation==0);
    b.effects.bounciness=100;b.motion.rotationSequence++;b.motion.rotationDirection=-1;
    state.update(b,1.12);state.update(b,1.17);assert(state.transform().rotation<0);
    b.effects.intensity=0;state.update(b,1.18);
    assert(state.impulses().empty() && state.transform().x==0 && state.transform().y==0 && state.transform().rotation==0);
    // Elasticity controls both excursion and return duration; shake is separate.
    float low=0,normal=0,high=0;
    for(int value:{50,100,200}) {
        Graphical::FieldEffects effect;Presentation::BoardView frame;frame.width=10;frame.bottom=20;
        frame.effects.bounciness=value;frame.effects.shake=0;effect.update(frame,0);
        frame.pieces=1;frame.lines=4;effect.update(frame,.01);effect.update(frame,.08);
        const auto displacement=effect.transform().y;assert(displacement>0);
        if(value==50)low=displacement;else if(value==100)normal=displacement;else high=displacement;
    }
    assert(normal>low*2 && high>normal && normal>.75f);
    // Revealing a hidden field never replays historical contact counters.
    Presentation::BoardView secret;secret.width=10;secret.bottom=20;secret.spatialEffects=false;
    Graphical::FieldEffects hidden;hidden.update(secret,0);
    secret.motion={9,8,-1,1};secret.spatialEffects=true;hidden.update(secret,.1);
    assert(hidden.impulses().empty());
    // A visible ordinary rotation must not imitate a confirmed Spin contact.
    Presentation::BoardView rotating;rotating.width=10;rotating.bottom=20;
    rotating.active=Presentation::PieceView{Piece::Type::from_index(5),{{0,4},Runtime::Rotation{}}};
    Graphical::FieldEffects ordinary;ordinary.update(rotating,0);
    rotating.active->pose.rotation=Runtime::Rotation(1);ordinary.update(rotating,.05);
    assert(ordinary.transform().rotation==0);
    // Incoming garbage alone excites damage shake, without elastic movement.
    b={};b.width=10;b.bottom=20;b.effects.bounciness=0;
    Graphical::FieldEffects damage;damage.update(b,0);b.garbage={8};damage.update(b,.01);damage.update(b,.05);
    assert(std::abs(damage.shake().x)+std::abs(damage.shake().y)>.03f && damage.transform().rotation==0);
    b.effects.shake=0;damage.update(b,.06);assert(damage.shake().x==0 && damage.shake().y==0);
}
int run(int argc,char** argv) {board_elasticity_contacts_and_strength();field_feedback_lifecycle();field_feedback_cadence_and_replay();fades_and_motion();retained_and_retriggered();spike_does_not_restart_on_unrelated_placements();
    spike_expires_after_one_and_a_half_seconds();
    pause_menu_finish_and_seek();new_round_drops_previous_events();cadence_and_boards();    return 0;
}
} // namespace TetrTests::graphical_event_motion_test
