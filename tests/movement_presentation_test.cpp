#include "tetr/compat/legacy_encoding.h"
#include "tetr/engine/movement_presentation.h"
#include "tetr/replay/movement_event.h"
#include "tetr/replay/operation_codec.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <queue>
#include <random>
#include <string>
#include <tuple>
#include <vector>

namespace TetrTests::movement_presentation_test {
namespace {

struct LegacyState {
    int row=0,col=0,rotation=0,lastRotationOp=0;
    int lockResetCount=0;
    double lockTimer=0;
};
struct LegacyEffect { bool changed=false; bool terminate=false; };

bool same(const LegacyState& a,const Engine::MovementState& b){
    return a.row==b.pose.position.row&&a.col==b.pose.position.col&&
           a.rotation==Compat::to_legacy(b.pose.rotation)&&a.lastRotationOp==Compat::to_legacy(b.lastRotation)&&
           a.lockResetCount==b.lockResetCount&&std::fabs(a.lockTimer-b.lockTimer.seconds())<1e-12;
}

LegacyEffect legacy_apply(
    LegacyState& s,char op,
    const std::function<bool(int,int,int)>& canPlace,
    const std::function<bool(int&,int&,int,int,int&)>& tryRotate) {
    LegacyEffect out;
    auto floor=[&]{
        while(canPlace(s.row+1,s.col,s.rotation)) {++s.row;s.lastRotationOp=0;out.changed=true;}
    };
    if(op=='H'){floor();out.terminate=true;return out;}
    auto rot=[&](int d){
        int r=s.row,c=s.col,k=0,t=(s.rotation+d+4)%4;
        if(tryRotate(r,c,s.rotation,t,k)){s.row=r;s.col=c;s.rotation=t;s.lastRotationOp=k+1;out.changed=true;}
    };
    if(op=='A') rot(2);
    if(op=='X') rot(1);
    if(op=='Z') rot(-1);
    if(op=='S'&&canPlace(s.row+1,s.col,s.rotation)){++s.row;s.lastRotationOp=0;out.changed=true;}
    if(op=='s') floor();
    if(op=='L'&&canPlace(s.row,s.col-1,s.rotation)){--s.col;s.lastRotationOp=0;out.changed=true;}
    if(op=='l') while(canPlace(s.row,s.col-1,s.rotation)){--s.col;s.lastRotationOp=0;out.changed=true;}
    if(op=='R'&&canPlace(s.row,s.col+1,s.rotation)){++s.col;s.lastRotationOp=0;out.changed=true;}
    if(op=='r') while(canPlace(s.row,s.col+1,s.rotation)){++s.col;s.lastRotationOp=0;out.changed=true;}
    return out;
}

void test_script_random_equivalence(){
    std::mt19937 rng(1234567); const std::string commands="HAXZSsLlRr?";
    for(int tc=0;tc<100000;++tc){
        bool blocked[16][10]{}; for(int r=0;r<16;++r)for(int c=0;c<10;++c)blocked[r][c]=(rng()%9==0);
        LegacyState a;
        a.row=int(rng()%12);a.col=int(rng()%10);a.rotation=int(rng()%4);a.lastRotationOp=int(rng()%8);
        a.lockResetCount=int(rng()%20);a.lockTimer=(rng()%1000)/100.0;
        Engine::MovementState b;
        b.pose={{a.row,a.col},Runtime::Rotation{a.rotation}};b.lastRotation=Compat::rotation_record_from_legacy(a.lastRotationOp);
        b.lockResetCount=a.lockResetCount;b.lockTimer=Runtime::TimePoint::from_seconds(a.lockTimer);
        std::function<bool(int,int,int)> can=[&](int r,int c,int){return r>=0&&r<16&&c>=0&&c<10&&!blocked[r][c];};
        std::function<bool(int&,int&,int,int,int&)> rot=[&](int& r,int& c,int,int target,int& kick){
            if(can(r,c,target)){kick=0;return true;} if(can(r,c+1,target)){++c;kick=1;return true;}
            if(can(r,c-1,target)){--c;kick=2;return true;} return false;
        };
        Engine::MovementController::CanPlace typedCan=[&](Piece::Pose pose){return can(pose.position.row,pose.position.col,Compat::to_legacy(pose.rotation));};
        Engine::MovementController::TryRotate typedRot=[&](Piece::Pose& pose,Runtime::Rotation target,Piece::KickIndex& kick){
            int r=pose.position.row,c=pose.position.col,k=0;
            if(!rot(r,c,Compat::to_legacy(pose.rotation),Compat::to_legacy(target),k))return false;
            pose.position={r,c};pose.rotation=target;kick=Piece::KickIndex{k};return true;
        };
        const char op=commands[rng()%commands.size()];
        auto le=legacy_apply(a,op,can,rot);
        auto ne=Engine::apply_movement_script_command(b,Engine::decode_movement_script_command(op),typedCan,typedRot);
        assert(same(a,b));assert(le.changed==ne.changed&&le.terminate==ne.terminate);
    }
}

void test_script_sequence_with_instant_gravity(){
    std::mt19937 rng(998244353); const std::string alphabet="AXZSsLlRrH";
    for(int tc=0;tc<20000;++tc){
        bool blocked[18][10]{};for(int r=0;r<18;++r)for(int c=0;c<10;++c)blocked[r][c]=(rng()%11==0);
        LegacyState oldState; oldState.row=int(rng()%10);oldState.col=int(rng()%10);oldState.rotation=int(rng()%4);oldState.lastRotationOp=int(rng()%5);
        Engine::MovementState newState;newState.pose={{oldState.row,oldState.col},Runtime::Rotation{oldState.rotation}};newState.lastRotation=Compat::rotation_record_from_legacy(oldState.lastRotationOp);
        std::function<bool(int,int,int)> can=[&](int r,int c,int){return r>=0&&r<18&&c>=0&&c<10&&!blocked[r][c];};
        std::function<bool(int&,int&,int,int,int&)> rot=[&](int& r,int& c,int,int target,int& kick){
            if(can(r,c,target)){kick=0;return true;}if(can(r,c+1,target)){++c;kick=1;return true;}
            if(can(r,c-1,target)){--c;kick=2;return true;}return false;
        };
        Engine::MovementController::CanPlace typedCan=[&](Piece::Pose pose){return can(pose.position.row,pose.position.col,Compat::to_legacy(pose.rotation));};
        Engine::MovementController::TryRotate typedRot=[&](Piece::Pose& pose,Runtime::Rotation target,Piece::KickIndex& kick){
            int r=pose.position.row,c=pose.position.col,k=0;if(!rot(r,c,Compat::to_legacy(pose.rotation),Compat::to_legacy(target),k))return false;
            pose.position={r,c};pose.rotation=target;kick=Piece::KickIndex{k};return true;
        };
        const bool instant=(rng()&1)!=0;
        if(instant){while(can(oldState.row+1,oldState.col,oldState.rotation)){++oldState.row;oldState.lastRotationOp=0;}Engine::apply_presentation_instant_gravity(newState,typedCan);}
        std::string script;for(int i=0,n=1+int(rng()%20);i<n;++i)script.push_back(alphabet[rng()%alphabet.size()]);
        for(char op:script){
            auto a=legacy_apply(oldState,op,can,rot);auto b=Engine::apply_movement_script_command(newState,Engine::decode_movement_script_command(op),typedCan,typedRot);
            assert(a.terminate==b.terminate);if(a.terminate)break;
            if(instant){while(can(oldState.row+1,oldState.col,oldState.rotation)){++oldState.row;oldState.lastRotationOp=0;}Engine::apply_presentation_instant_gravity(newState,typedCan);}
            assert(same(oldState,newState));
        }
        assert(same(oldState,newState));
    }
}

void test_playback_timing(){
    Engine::MovementScriptPlayback p("LXR",Runtime::TimePoint::from_seconds(10.0),Runtime::Duration::from_seconds(0.25));
    assert(p.commandDue(Runtime::TimePoint::from_seconds(10.0)));assert(p.consume()==Engine::MovementScriptCommand::LeftStep);
    assert(!p.commandDue(Runtime::TimePoint::from_seconds(10.24)));assert(p.commandDue(Runtime::TimePoint::from_seconds(10.25)));assert(p.consume()==Engine::MovementScriptCommand::RotateCW);
    assert(!p.commandDue(Runtime::TimePoint::from_seconds(10.49)));assert(p.commandDue(Runtime::TimePoint::from_seconds(10.50)));assert(p.consume()==Engine::MovementScriptCommand::RightStep);assert(p.finished());
}

void test_replay_events(){
    using Replay::MovementEventType;
    auto move=Replay::parse_movement_event(Replay::decode_operation_fields({"move","1.25","-2","4","3"}));
    assert(move.type==MovementEventType::Move);assert(std::fabs(move.timestamp.seconds()-1.25)<1e-12);
    assert(move.pose.position.row==-2&&move.pose.position.col==4&&Compat::to_legacy(move.pose.rotation)==3);
    auto end=Replay::parse_movement_event(Replay::decode_operation_fields({"end_move_data","2.50","5","12345","7","2","1"}));
    assert(end.type==MovementEventType::EndMoveData);assert(Compat::to_legacy(end.lastRotation)==5&&end.score==12345);
    assert(end.pose.position.row==7&&end.pose.position.col==2&&Compat::to_legacy(end.pose.rotation)==1);
    std::queue<Replay::Operation> q;q.push(Replay::decode_operation_fields({"start_place","3.00"}));Replay::QueuedEvent out;
    assert(!Replay::pop_due(q,Runtime::Duration::from_seconds(2.99),out));assert(q.size()==1);assert(Replay::pop_due(q,Runtime::Duration::from_seconds(3.00),out));
    assert(out.movement.type==MovementEventType::StartPlace);assert(q.empty());
}

void test_instant_horizontal_commands() {
    Engine::MovementState state;
    state.pose={{0,4},Runtime::Rotation{}};
    const auto canPlace=[](Piece::Pose pose) {
        return pose.position.col>=0 && pose.position.col<10 && pose.position.col!=8;
    };
    const auto rotate=[](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) { return false; };
    auto effect=Engine::apply_movement_script_command(state,Engine::MovementScriptCommand::LeftStep,canPlace,rotate,true,true);
    assert(effect.changed && state.pose.position.col==0);
    effect=Engine::apply_movement_script_command(state,Engine::MovementScriptCommand::RightStep,canPlace,rotate,true,true);
    assert(effect.changed && state.pose.position.col==7);
    effect=Engine::apply_movement_script_command(state,Engine::MovementScriptCommand::RightStep,canPlace,rotate,true,true);
    assert(!effect.changed && state.pose.position.col==7);
    effect=Engine::apply_movement_script_command(state,Engine::MovementScriptCommand::LeftStep,canPlace,rotate);
    assert(effect.changed && state.pose.position.col==6);
    effect=Engine::apply_movement_script_command(state,Engine::MovementScriptCommand::LeftStep,canPlace,rotate,false,true);
    assert(effect.changed && state.pose.position.col==5); // ARR=0 still permits taps
    effect=Engine::apply_movement_script_command(state,Engine::MovementScriptCommand::LeftStep,canPlace,rotate,true,false);
    assert(effect.changed && state.pose.position.col==3); // immediate first repeat at DAS=0
}

void test_each_cell_settles_before_the_next_shift() {
    for (int direction:{-1,1}) for (bool zeroDas:{false,true}) for (bool zeroArr:{false,true}) {
        const auto column=[&](int logical) { return direction>0 ? logical : 8-logical; };
        const auto canPlace=[&](Piece::Pose pose) {
            if (pose.position.col<0 || pose.position.col>8) return false;
            const int logical=column(pose.position.col);
            return pose.position.row<=(logical==3 ? 10 : 5);
        };
        Engine::MovementState state;state.pose={{5,column(2)},Runtime::Rotation{}};
        std::vector<std::tuple<char,int,int>> records;
        const auto rotate=[](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) {return false;};
        const auto recorder=[&](char code,int rows,int columns) { records.emplace_back(code,rows,columns); };
        const auto command=direction<0 ? Engine::MovementScriptCommand::LeftStep : Engine::MovementScriptCommand::RightStep;
        auto effect=Engine::apply_movement_script_command(state,command,canPlace,rotate,
            zeroDas,zeroArr,{},true,true,recorder);
        assert(effect.changed && state.pose.position.row==10 && state.pose.position.col==column(3));
        // Independently replay each recorded cell, then its vertical descent.
        Piece::Pose recorded{{5,column(2)},Runtime::Rotation{}};
        bool sawDrop=false;
        for (const auto& [code,rows,columns]:records) {
            if (code=='L' || code=='R') {
                assert(rows==0 && std::abs(columns)<=1);
                recorded=recorded.translated(0,columns);assert(canPlace(recorded));
            } else {
                assert(code=='s' && columns==0);
                for(int cell=0;cell<rows;++cell) {
                    recorded=recorded.translated(1,0);assert(canPlace(recorded));
                }
                assert(!canPlace(recorded.translated(1,0)));sawDrop=true;
            }
        }
        assert(sawDrop && recorded==state.pose);
        assert(!Engine::apply_movement_script_command(state,command,canPlace,rotate,
            zeroDas,zeroArr,{},true,true).changed);
    }
}
} // namespace
int run(int argc,char** argv){test_script_random_equivalence();test_script_sequence_with_instant_gravity();test_playback_timing();test_replay_events();test_instant_horizontal_commands();test_each_cell_settles_before_the_next_shift();std::cout<<"movement presentation tests passed\n";    return 0;
}
} // namespace TetrTests::movement_presentation_test
