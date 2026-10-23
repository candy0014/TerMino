#include "tetr/compat/legacy_encoding.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/engine/player_input.h"
#include "tetr/rules/blitz.h"
#include "tetr/rules/game_completion.h"
#include "tetr/replay/replay_hash.h"

#include <cassert>
#include <cmath>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace TetrTests::periodic_and_input_test {
namespace {

bool near(double a, double b) { return std::fabs(a-b) < 1e-12; }
constexpr Runtime::TimePoint tp(double value){ return Runtime::TimePoint::from_seconds(value); }
constexpr Runtime::Duration dur(double value){ return Runtime::Duration::from_seconds(value); }

void test_replay_hash() {
    assert(Replay::legacy_replay_hash_modulus()==10000000000002136LL);
    assert(Replay::replay_hash_suffix("")=="00000000000000000");
    std::mt19937 rng(998244353);
    for(int tc=0;tc<10000;tc++){
        std::string str;
        int n=rng()%200;
        for(int i=0;i<n;i++) str.push_back(char(32+rng()%95));
        long long mod=1e16+2137,base=233,hash=0;
        for(int i=0;str[i];i++) hash=(hash*base+(int)str[i])%mod;
        std::string expected=std::to_string(hash);
        while(expected.length()<17) expected="0"+expected;
        assert(Replay::replay_hash_suffix(str)==expected);
    }
}

void test_completion() {
    std::mt19937 rng(114514);
    for(int i=0;i<100000;i++){
        int model=rng()%4,gm=rng()%12,lines=rng()%200,garbage=rng()%200,distance=1+rng()%100;
        bool old=false;
        if(model==1&&lines>=distance){
            if(gm==3||gm==4) old=garbage>=distance;
            else old=lines>=distance;
        }
        assert(old==Rules::racing_goal_reached(
            Compat::game_mode_from_legacy(model),
            Compat::garbage_mode_from_legacy(gm),
            lines,garbage,distance));
        double start=(rng()%10000)/100.0,now=start+(rng()%10000)/100.0,limit=(rng()%10000)/100.0;
        assert((model==2&&now-start>limit)==Rules::blitz_goal_reached(
            Compat::game_mode_from_legacy(model),dur(now-start),dur(limit)));
    }
}

void test_blitz() {
    for(int lines=0;lines<100000;lines++){
        int tmp=lines, old=0;
        for(int i=1;;i++){
            if(tmp>i*2+(i+9)/10) tmp-=i*2+(i+9)/10;
            else { old=i; break; }
        }
        assert(old==Rules::blitz_level_from_lines(lines));
    }
    assert(Rules::blitz_gravity_for_level(1).seconds()==1.0);
    assert(Rules::blitz_gravity_for_level(15).seconds()==0.0);
    assert(Rules::blitz_gravity_for_level(100).seconds()==0.0);
}

void test_periodic() {
    assert(!Engine::periodic_event_due(tp(1.0), tp(0.0), dur(1.0)));
    assert(Engine::periodic_event_due(tp(1.0001), tp(0.0), dur(1.0)));
    assert(Engine::autoplay_step_due(true, 10.0, tp(0.0), tp(100.0)));
    assert(Engine::autoplay_step_due(false, 0.0, tp(0.0), tp(100.0)));
    assert(!Engine::autoplay_step_due(false, 5.0, tp(0.2), tp(0.0)));
    assert(Engine::autoplay_step_due(false, 5.0, tp(0.2001), tp(0.0)));
    assert(Engine::blitz_time_expired(Runtime::GameMode::Blitz, dur(60.1), dur(60.0)));
    assert(!Engine::blitz_time_expired(Runtime::GameMode::Racing, dur(100.0), dur(60.0)));

    std::mt19937 rng(123456);
    std::uniform_real_distribution<double> d(0.0, 1e9);
    for (int i=0;i<100000;i++) {
        const double s=(i%97==0)?1e9+5.0:d(rng);
        const Runtime::GravityInterval gravity=Compat::gravity_from_legacy_seconds(s);
        for (bool guard : {false,true}) {
            double old=s;
            if (!(guard && old>1e9)) {
                if(old>1e-8) old=1/(1/old+0.03);
                if(old*1200<1) old=0;
            }
            assert(near(old, Compat::to_legacy_seconds(Engine::advance_qp_gravity_speed(gravity,guard))));
        }
    }
}

void test_collect_keys() {
    std::array<int, Input::kBindableActionCount> keyCodes{};
    keyCodes.fill(-1);
    keyCodes[4]=1; keyCodes[0]=2; keyCodes[5]=3; keyCodes[1]=4; keyCodes[2]=5;
    const auto typed=Input::BindingSet::from_key_codes(keyCodes);
    assert((Engine::collect_action_keys(typed,{Input::Action::Rotate180,Input::Action::RotateClockwise,Input::Action::RotateCounterclockwise})==std::vector<int>{1,3}));
    assert((Engine::collect_action_keys(typed,{Input::Action::MoveLeft,Input::Action::MoveRight})==std::vector<int>{2,4}));
}

void test_input_basic() {
    Engine::MovementState st;
    st.pose={{0,0},Runtime::Rotation{0}}; st.lockTimer=tp(0);
    auto canPlace=[](Piece::Pose pose){ return pose.position.row>=0 && pose.position.row<=4 && pose.position.col>=-2 && pose.position.col<=2; };
    auto rotate=[](Piece::Pose& pose,Runtime::Rotation target,Piece::KickIndex& kick){ kick=Piece::KickIndex{0}; pose.rotation=target; return true; };
    Engine::MovementController movement(st,canPlace,rotate);
    Engine::MovementInputState input;
    std::array<int, Input::kBindableActionCount> keyCodes{};
    keyCodes.fill(-1);
    keyCodes[0]=1; keyCodes[1]=2; keyCodes[2]=3; keyCodes[3]=4; keyCodes[4]=5;
    keyCodes[7]=6; keyCodes[8]=7; keyCodes[9]=8; keyCodes[6]=9;
    const auto typedBindings=Input::BindingSet::from_key_codes(keyCodes);
    auto rot=Engine::collect_action_keys(typedBindings,{Input::Action::Rotate180,Input::Action::RotateClockwise,Input::Action::RotateCounterclockwise});
    auto mov=Engine::collect_action_keys(typedBindings,{Input::Action::MoveLeft,Input::Action::MoveRight});
    std::map<int,bool> down;
    double t=1.0;
    auto keyDown=[&](int k){return down[k];};
    auto now=[&](){return tp(t);};
    Engine::PlayerInputConfig cfg;
    cfg.holdEnabled=true; cfg.lockResetLimit=15; cfg.gravityInterval=Runtime::GravityInterval::periodic(dur(1)); cfg.softDropFactor=1; cfg.das=dur(0.1); cfg.arr=dur(0.05);
    input.preparePiece(typedBindings,keyDown,tp(t));

    down[1]=true;
    auto r=Engine::process_player_input_frame(input,movement,typedBindings,rot,mov,cfg,keyDown,now);
    assert(r.command==Engine::PlayerInputCommand::None && st.pose.position.col==-1);
    down[1]=false;
    Engine::process_player_input_frame(input,movement,typedBindings,rot,mov,cfg,keyDown,now);

    down[3]=true; t+=0.01;
    r=Engine::process_player_input_frame(input,movement,typedBindings,rot,mov,cfg,keyDown,now);
    assert(r.scoreDelta==1 && st.pose.position.row==1);
    down[3]=false;
    Engine::process_player_input_frame(input,movement,typedBindings,rot,mov,cfg,keyDown,now);

    down[5]=true; t+=0.01;
    r=Engine::process_player_input_frame(input,movement,typedBindings,rot,mov,cfg,keyDown,now);
    assert(Compat::to_legacy(st.pose.rotation)==1);
    down[5]=false;
    Engine::process_player_input_frame(input,movement,typedBindings,rot,mov,cfg,keyDown,now);

    down[6]=true; t+=0.01;
    r=Engine::process_player_input_frame(input,movement,typedBindings,rot,mov,cfg,keyDown,now);
    assert(r.command==Engine::PlayerInputCommand::Hold);
}

} // namespace

int run(int argc,char** argv){ test_replay_hash(); test_completion(); test_blitz(); test_periodic(); test_collect_keys(); test_input_basic();     return 0;
}
} // namespace TetrTests::periodic_and_input_test
