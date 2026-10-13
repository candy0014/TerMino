#include "tetr/compat/legacy_encoding.h"
#include "tetr/engine/player_input.h"
#include "tetr/engine/movement_input_state.h"

#include <cassert>
#include <cmath>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

using Engine::PlayerInputCommand;

constexpr Runtime::TimePoint tp(double value){
    return Runtime::TimePoint::from_seconds(value);
}

constexpr Runtime::Duration dur(double value){
    return Runtime::Duration::from_seconds(value);
}

// Historical input dispatch oracle, with elapsed repeats counted to match the
// frame-independent DAS/ARR and soft-drop contract.
Engine::PlayerInputResult reference_process(
    Engine::MovementInputState& input,
    Engine::MovementController& movement,
    const std::vector<int>& keys,
    const std::map<int,std::string>& bindings,
    const std::vector<int>& rotateKeys,
    const std::vector<int>& moveKeys,
    const Engine::PlayerInputConfig& config,
    const std::map<int,bool>& down,
    double now) {
    Engine::PlayerInputResult result;
    for(auto ii:keys){
        int i=(int)ii;
        const auto it=bindings.find(i);
        if(it==bindings.end()) continue;
        const std::string& action=it->second;
        if(config.autoplay&&action!="RE"&&action!="SET") continue;

        const bool isDown=down.count(i)?down.at(i):false;
        if(input.consumeReleaseDebounce(i,isDown)) continue;
        if(static_cast<int>(input.phase(i))==0&&!isDown) continue;

        if(isDown&&action=="HD"){
            const auto effect=movement.dropToFloor(2,false,tp(now));
            result.scoreDelta+=effect.scoreDelta;
            result.command=PlayerInputCommand::HardDrop;
            return result;
        }
        if(isDown&&action=="RE"){
            result.command=PlayerInputCommand::Retry;
            return result;
        }
        if(isDown&&action=="SET"){
            result.command=PlayerInputCommand::OpenSettings;
            return result;
        }
        if(input.consumeRelease(i,isDown)) continue;

        const Runtime::TimePoint inputNow=tp(now);
        if(input.beginPress(i,isDown,inputNow)){
            if(action=="H"){
                if(config.holdAlreadyUsed||!config.holdEnabled) continue;
                result.command=PlayerInputCommand::Hold;
                return result;
            }
            if((action=="F"||action=="CCW"||action=="CW")&&
               input.mostRecentActive(rotateKeys)==i){
                int delta=0;
                if(action=="F") delta=2;
                if(action=="CCW") delta=-1;
                if(action=="CW") delta=1;
                movement.rotate(delta,inputNow,config.lockResetLimit);
            }
            if(action=="SD"){
                const auto effect=Engine::soft_drop_interval(config.gravityInterval,config.softDropFactor)==dur(0)
                    ? movement.dropToFloor(1,true,inputNow) : movement.softDropStep(inputNow);
                result.scoreDelta+=effect.scoreDelta;
            }
            if((action=="L"||action=="R")&&
               input.mostRecentActive(moveKeys)==i){
                movement.moveHorizontal(action=="L"?-1:1,inputNow,config.lockResetLimit);
            }
        }
        else if(static_cast<int>(input.phase(i))>0&&isDown){
            if(action=="CW"||action=="CCW"||action=="F") continue;
            if(action=="SD"){
                if(Engine::soft_drop_interval(config.gravityInterval,config.softDropFactor)==dur(0)){
                    const auto effect=movement.dropToFloor(1,true,inputNow);
                    result.scoreDelta+=effect.scoreDelta;
                }
                else {
                    const int count=input.softDropRepeatsDue(i,inputNow,config.gravityInterval,config.softDropFactor);
                    for (int repeat=0;repeat<count;++repeat) {
                        const auto effect=movement.softDropStep(inputNow);
                        result.scoreDelta+=effect.scoreDelta;
                        if (!effect.changed) break;
                    }
                }
            }
            if((action=="L"||action=="R")&&
               input.mostRecentActive(moveKeys)==i){
                const int count=input.horizontalRepeatsDue(i,inputNow,config.das,config.arr);
                for (int repeat=0;repeat<count;++repeat)
                    if (!movement.moveHorizontal(action=="L"?-1:1,inputNow,config.lockResetLimit).changed) break;
            }
        }
    }
    return result;
}

bool same_state(const Engine::MovementState& a,const Engine::MovementState& b){
    return a.pose.position.row==b.pose.position.row&&a.pose.position.col==b.pose.position.col&&a.pose.rotation==b.pose.rotation&&
           a.lastRotation==b.lastRotation&&a.lockResetCount==b.lockResetCount&&
           std::fabs((a.lockTimer-b.lockTimer).seconds())<1e-12;
}

void run_random_equivalence(){
    std::mt19937 rng(20260831);
    // Keep the synthetic menu-action keys outside reserved navigation codes.
    const std::vector<int> keys={1,2,3,4,5,6,7,8,11,12};
    std::array<int, Input::kBindableActionCount> keyCodes{};
    keyCodes.fill(-1);
    keyCodes[0]=1; keyCodes[1]=2; keyCodes[2]=3; keyCodes[3]=4; keyCodes[4]=5;
    keyCodes[5]=6; keyCodes[6]=7; keyCodes[7]=8; keyCodes[8]=11; keyCodes[9]=12;
    const std::map<int,std::string> bindings={
        {1,"L"},{2,"R"},{3,"SD"},{4,"HD"},{5,"CW"},
        {6,"CCW"},{7,"F"},{8,"H"},{11,"RE"},{12,"SET"}
    }; // historical oracle only
    const auto typedBindings=Input::BindingSet::from_key_codes(keyCodes);
    const auto rotateKeys=Engine::collect_action_keys(typedBindings,{Input::Action::Rotate180,Input::Action::RotateClockwise,Input::Action::RotateCounterclockwise});
    const auto moveKeys=Engine::collect_action_keys(typedBindings,{Input::Action::MoveLeft,Input::Action::MoveRight});

    for(int tc=0;tc<4000;tc++){
        Engine::MovementState a,b;
        const int row=int(rng()%8), col=int(rng()%5)-2, rotation=int(rng()%4), lastRotation=int(rng()%7);
        a.pose=b.pose={{row,col},Runtime::Rotation{rotation}};
        a.lastRotation=b.lastRotation=Compat::rotation_record_from_legacy(lastRotation);
        a.lockResetCount=b.lockResetCount=int(rng()%20);
        a.lockTimer=b.lockTimer=tp(0.0);
        Engine::MovementInputState ia,ib;
        std::map<int,bool> down;
        double time=1.0;
        auto keyDown=[&](int k){return down[k];};
        ia.preparePiece(typedBindings,keyDown,tp(time));
        ib.preparePiece(typedBindings,keyDown,tp(time));

        auto canPlace=[](Piece::Pose pose){
            return pose.position.row>=0&&pose.position.row<=18&&pose.position.col>=-4&&pose.position.col<=4;
        };
        auto rotate=[](Piece::Pose& pose,Runtime::Rotation to,Piece::KickIndex& kick){
            kick=Piece::KickIndex{(Compat::to_legacy(pose.rotation)+Compat::to_legacy(to)+8)%3};
            if(pose.position.col==4) --pose.position.col;
            if(pose.position.row==18) --pose.position.row;
            pose.rotation=to;
            return true;
        };
        Engine::MovementController ma(a,canPlace,rotate),mb(b,canPlace,rotate);

        Engine::PlayerInputConfig cfg;
        cfg.autoplay=(rng()%10==0);
        cfg.holdAlreadyUsed=(rng()%3==0);
        cfg.holdEnabled=(rng()%5!=0);
        // Infinite falling now runs after rotations and horizontal input.
        // The real-board regressions in handling_controls cover that new
        // contract; this historical oracle retains ordinary finite rates.
        const double sdfOptions[3]={1.0,5.0,20.0};
        const double speedOptions[3]={0.01,0.1,1.0};
        cfg.softDropFactor=sdfOptions[rng()%3];
        cfg.gravityInterval=Compat::gravity_from_legacy_seconds(speedOptions[rng()%3]);
        // Zero handling intentionally differs from the historical one-step
        // oracle and is covered separately by the instant-movement regressions.
        cfg.das=dur((1+rng()%99)/500.0);
        cfg.arr=dur((1+rng()%99)/1000.0);
        cfg.lockResetLimit=rng()%30;

        for(int step=0;step<40;step++){
            time+=(rng()%20)/1000.0;
            for(int key:keys) down[key]=(rng()%100)<18;
            const auto old=reference_process(ia,ma,keys,bindings,rotateKeys,moveKeys,cfg,down,time);
            const auto neo=Engine::process_player_input_frame(
                ib,mb,typedBindings,rotateKeys,moveKeys,cfg,
                [&](int k){return down[k];},[&](){return tp(time);});
            assert(old.command==neo.command);
            assert(old.scoreDelta==neo.scoreDelta);
            assert(same_state(a,b));
            for(int key:keys){
                assert(ia.phase(key)==ib.phase(key));
                assert(std::fabs((ia.timestamp(key)-ib.timestamp(key)).seconds())<1e-12);
            }
        }
    }
}

void zero_horizontal_input() {
    std::array<int, Input::kBindableActionCount> codes{};
    codes.fill(-1); codes[0]=1; codes[1]=2;
    const auto bindings=Input::BindingSet::from_key_codes(codes);
    const std::vector<int> moveKeys{1,2};
    for (int width:{4,10,20}) for (int direction:{-1,1})
        for (bool grounded:{false,true}) {
            Engine::MovementState state;
            state.pose={{grounded ? 18 : 8,width/2},Runtime::Rotation{}};
            state.lastRotation=Runtime::RotationRecord::from_kick(Piece::KickIndex{2});
            state.lockTimer=tp(-1);
            Engine::MovementInputState input;
            auto canPlace=[&](Piece::Pose pose) {
                return pose.position.row<=18 && pose.position.col>=0 && pose.position.col<width;
            };
            Engine::MovementController movement(state,canPlace,
                [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) { return false; });
            Engine::PlayerInputConfig config;
            config.das=dur(0);
            config.arr=dur(0);
            config.lockResetLimit=1;
            const int key=direction<0 ? 1 : 2;
            bool down=true;
            const auto frame=[&] {
                return Engine::process_player_input_frame(input,movement,bindings,{},moveKeys,config,
                    [&](int pressed) { return down && pressed==key; },[] { return tp(0); });
            };
            assert(frame().scoreDelta==0);
            assert(state.pose.position.col==(direction<0 ? 0 : width-1));
            assert(state.pose.position.row==(grounded ? 18 : 8));
            assert(state.lockResetCount==(grounded ? 1 : 0));
            assert(state.lockTimer==tp(grounded ? 0 : -1));
            assert(state.lastRotation==Runtime::RotationRecord::none());
            frame(); // A stationary clock still accepts zero-time repeat.
            assert(input.phase(key)==Engine::KeyPhase::Repeating);
            assert(state.lockResetCount==(grounded ? 1 : 0)); // blocked: no reset

            // DAS carry must move a new piece all the way on its first frame.
            state.pose.position.col=width/2;
            input.preparePiece(bindings,[&](int pressed) { return pressed==key; },tp(0));
            frame();
            assert(state.pose.position.col==(direction<0 ? 0 : width-1));
            assert(state.lockResetCount==(grounded ? 2 : 0));
            assert(state.lockTimer==tp(grounded ? 0 : -1));
            down=false; frame();
            assert(input.phase(key)==Engine::KeyPhase::Released);
        }

    // Stop at the first obstacle, and commit one lock reset for the entire
    // displacement. Moving along the floor again cannot bypass the reset cap.
    Engine::MovementState state;
    state.pose={{18,5},Runtime::Rotation{}};
    auto canPlace=[](Piece::Pose pose) {
        return pose.position.row<=18 && pose.position.col>=0 && pose.position.col<10 &&
            pose.position.col!=1 && pose.position.col!=8;
    };
    Engine::MovementController movement(state,canPlace,
        [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) { return false; });
    assert(movement.moveHorizontalToWall(-1,tp(1),1).changed);
    assert(state.pose.position.col==2 && state.lockResetCount==1 && state.lockTimer==tp(1));
    assert(movement.moveHorizontalToWall(1,tp(2),1).changed);
    assert(state.pose.position.col==7 && state.lockResetCount==2 && state.lockTimer==tp(1));
    state.lastRotation=Runtime::RotationRecord::from_kick(Piece::KickIndex{1});
    assert(!movement.moveHorizontalToWall(1,tp(3),1).changed);
    assert(state.lastRotation!=Runtime::RotationRecord::none());
    assert(state.lockResetCount==2 && state.lockTimer==tp(1));

    // Positive DAS/ARR keep the ordinary first-tap single step, including at
    // game time zero. Switching directions immediately honors the newest key.
    Engine::MovementInputState input;
    Engine::PlayerInputConfig config;
    config.das=dur(0.1); config.arr=dur(0.01);
    state.pose.position.col=5;
    auto frame=[&](int key, double time) {
        Engine::process_player_input_frame(input,movement,bindings,{},moveKeys,config,
            [&](int pressed) { return pressed==key; },[&] { return tp(time); });
    };
    frame(1,0); assert(state.pose.position.col==4);
    config.arr=dur(0);
    frame(2,0.01); assert(state.pose.position.col==5);
    frame(1,0.02); assert(state.pose.position.col==4);
}

void guideline_horizontal_timing() {
    std::array<int, Input::kBindableActionCount> codes{};
    codes.fill(-1); codes[0]=1; codes[1]=2;
    const auto bindings=Input::BindingSet::from_key_codes(codes);
    Engine::MovementState state;
    Engine::MovementInputState input;
    Engine::MovementController movement(state,
        [](Piece::Pose pose) { return pose.position.row<=18 && pose.position.col>=0 && pose.position.col<10; },
        [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) { return false; });
    Engine::PlayerInputConfig config;
    config.lockResetLimit=15;
    const auto reset=[&](double das, double arr) {
        input.clear(); state={}; state.pose={{18,4},Runtime::Rotation{}};
        config.das=dur(das); config.arr=dur(arr);
    };
    const auto frame=[&](double time, int key=1) {
        Engine::process_player_input_frame(input,movement,bindings,{},{1,2},config,
            [&](int pressed) { return pressed==key; },[&] { return tp(time); });
    };
    // Nonzero DAS + zero ARR permits taps until DAS finishes, then slides in
    // that same frame without spending one frame per cell.
    reset(0.25,0);
    frame(1); assert(state.pose.position.col==3 && state.lockResetCount==1);
    frame(1.24); assert(state.pose.position.col==3);
    frame(1.25); assert(state.pose.position.col==0 && state.lockResetCount==2);
    frame(1.25); assert(state.lockResetCount==2);
    state.pose.position.col=4;
    input.preparePiece(bindings,[](int key) { return key==1; },tp(1.25));
    frame(1.25); assert(state.pose.position.col==0 && state.lockResetCount==3);
    frame(1.5,2); assert(state.pose.position.col==1); // direction change charges again
    frame(1.75,2); assert(state.pose.position.col==9);

    // Guideline's first repeat is at DAS, not DAS+ARR.
    reset(0.25,0.5);
    frame(1); assert(state.pose.position.col==3);
    frame(1.25); assert(state.pose.position.col==2);
    frame(1.74); assert(state.pose.position.col==2);
    frame(1.75); assert(state.pose.position.col==1);

    // Zero DAS means the initial tap and first automatic shift happen in
    // the same frame; subsequent shifts still honor nonzero ARR.
    reset(0,0.25);
    frame(0); assert(state.pose.position.col==2 && input.phase(1)==Engine::KeyPhase::Repeating);
    frame(0); assert(state.pose.position.col==2);
    frame(0.24); assert(state.pose.position.col==2);
    frame(0.25); assert(state.pose.position.col==1);
    frame(0.5); assert(state.pose.position.col==0);
    frame(0.5,2); assert(state.pose.position.col==2);

    reset(0,0);
    frame(0); assert(state.pose.position.col==0 && state.lockResetCount==1);
    frame(0,2); assert(state.pose.position.col==9 && state.lockResetCount==2);
    // At a stationary timestamp, the freshly pressed opposite direction
    // wins even if the old direction remains held.
    reset(0,0);
    frame(0);
    Engine::process_player_input_frame(input,movement,bindings,{},{1,2},config,
        [](int key) { return key==1 || key==2; },[] { return tp(0); });
    assert(state.pose.position.col==9);
}

} // namespace

int main(){ run_random_equivalence(); zero_horizontal_input(); guideline_horizontal_timing(); }
