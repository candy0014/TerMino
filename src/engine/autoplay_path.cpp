#include "tetr/engine/autoplay_path.h"
#include "tetr/engine/player_input.h"
#include "tetr/rules/spin.h"
#include <algorithm>
#include <map>
#include <queue>
#include <tuple>
namespace Engine {
namespace {
using D=Runtime::Duration;
struct PresentationState {MovementState movement;int direction=0;bool repeated=false;};
struct Edge {PresentationState state;std::vector<AutoplayTimedStep> steps;D duration;bool terminated=false;};
using Key=std::tuple<int,int,int,int,int,bool>;
Key key(const PresentationState& state) {
    const auto& m=state.movement;
    return {m.pose.position.row,m.pose.position.col,m.pose.rotation.index(),
        m.lastRotation.rotated() ? m.lastRotation.kick().index()+1 : 0,state.direction,state.repeated};
}
D horizontal_delay(PresentationState& state,int direction,const Runtime::HandlingConfig& h) {
    D delay;
    if(state.direction==direction) {delay=state.repeated && h.arr>D{} ? h.arr : h.das;state.repeated=true;}
    else {if(state.direction) delay=h.das;state.direction=direction;state.repeated=false;}
    return delay;
}
Edge transition(PresentationState state,MovementScriptCommand command,const Runtime::Playfield& field,
    const Piece::Definition& piece,const Runtime::HandlingConfig& h,const Runtime::MovementRestrictions& r,
    Runtime::GravityInterval gravity,bool timed) {
    Edge edge{std::move(state),{}, {},false};
    const auto canPlace=[&](Piece::Pose pose){return piece.can_place(pose,field);};
    const auto tryRotate=[&](Piece::Pose& pose,Runtime::Rotation rotation,Piece::KickIndex& kick) {
        return piece.try_rotate(pose,rotation,field,kick);
    };
    if(command!=MovementScriptCommand::HardDrop && command!=MovementScriptCommand::WaitForLock &&
        !movement_script_command_allowed(command,r,h.das==D{},gravity.enabled(),gravity.is_instant(),
            soft_drop_interval(gravity,h.softDropFactor,h.softDropFixed)==D{})) return edge;
    const auto append=[&](MovementScriptCommand action,D delay) {
        const auto before=edge.state.movement.pose;
        if(action==MovementScriptCommand::HardDrop && r.disableHardDrop) return false;
        if(action==MovementScriptCommand::WaitForLock) {
            if(!gravity.enabled() || canPlace(before.translated(1,0))) return false;
            edge.steps.push_back({action,edge.state.movement,timed ? h.epld : D{},0});
            edge.duration+=edge.steps.back().delay;edge.terminated=true;return true;
        }
        const auto effect=apply_movement_script_command(edge.state.movement,action,canPlace,tryRotate,
            false,false,r,gravity.is_instant(),soft_drop_interval(gravity,h.softDropFactor,h.softDropFixed)==D{});
        if(!effect.changed && !effect.terminate) return false;
        if(!timed) delay={};
        const int rows=std::max(0,edge.state.movement.pose.position.row-before.position.row);
        const int score=action==MovementScriptCommand::HardDrop ? 2*rows :
            action==MovementScriptCommand::SoftDropStep || action==MovementScriptCommand::SoftDropToFloor ? rows : 0;
        edge.steps.push_back({action,edge.state.movement,delay,score});edge.duration+=delay;
        edge.terminated=effect.terminate;return true;
    };
    const auto parts=movement_script_components(command);
    const auto move=parts[1]!=MovementScriptCommand::Invalid ? parts[1] : parts[0];
    const bool composite=parts[1]!=MovementScriptCommand::Invalid;
    const bool wall=move==MovementScriptCommand::LeftToWall || move==MovementScriptCommand::RightToWall;
    const bool horizontal=wall || move==MovementScriptCommand::LeftStep || move==MovementScriptCommand::RightStep;
    const D rotationDelay=D::from_seconds(1./60)+(edge.state.repeated ? h.dcd : D{});
    if(horizontal) {
        const int direction=move==MovementScriptCommand::LeftStep || move==MovementScriptCommand::LeftToWall ? -1 : 1;
        if(!r.allows_horizontal(direction)) return edge;
        if(composite) {
            // A buffered turn and first shift share a frame before 20G falls.
            const auto effect=apply_movement_script_command(edge.state.movement,parts[0],canPlace,tryRotate,
                false,false,r,false,false);
            if(!effect.changed && !canPlace(edge.state.movement.pose.translated(0,direction))) return edge;
        }
        const auto action=direction<0 ? MovementScriptCommand::LeftStep : MovementScriptCommand::RightStep;
        for(int i=0;canPlace(edge.state.movement.pose.translated(0,direction));++i) {
            auto delay=horizontal_delay(edge.state,direction,h);
            if(composite && i==0) delay=std::max(delay,rotationDelay);
            // Charged ARR=0 slides move to the wall together; intermediate
            // taps still occupy DAS intervals.
            if(wall && h.arr==D{} && edge.state.repeated && i>1) delay={};
            if(!append(action,delay)) break;
            if(composite && i==0) edge.steps.back().command=command;
            if(!wall) break;
        }
        if(composite && edge.steps.empty()) {
            auto movement=edge.state.movement;
            apply_presentation_instant_gravity(movement,canPlace);
            edge.state.movement=movement;
            edge.steps.push_back({command,movement,timed ? rotationDelay : D{},0});
            edge.duration=edge.steps.back().delay;
        }
    } else if(command==MovementScriptCommand::SoftDropToFloor) {
        const auto interval=soft_drop_interval(gravity,h.softDropFactor,h.softDropFixed);
        if(interval==D{}) append(command,{});
        else while(canPlace(edge.state.movement.pose.translated(1,0)))
            if(!append(MovementScriptCommand::SoftDropStep,interval)) break;
    } else {
        const auto delay=command==MovementScriptCommand::SoftDropStep
            ? soft_drop_interval(gravity,h.softDropFactor,h.softDropFixed)
            : command==MovementScriptCommand::GravityStep ? gravity.interval()
            : command==MovementScriptCommand::RotateCW || command==MovementScriptCommand::RotateCCW ||
              command==MovementScriptCommand::Rotate180 ? rotationDelay : D{};
        append(command,delay);
    }
    return edge;
}
}
AutoplaySpinRules autoplay_spin_rules(const Runtime::RuntimeConfig& config) {
    return {config.spinBonuses,Runtime::enabled(config.quickPlay.allSpin),
        Runtime::reverse(config.quickPlay.noHold),Runtime::reverse(config.quickPlay.doubleHole)};
}
bool autoplay_landing_matches(const Runtime::Playfield& field,const Piece::Definition& piece,
    const MovementState& actual,Piece::Pose target,Runtime::RotationRecord rotation,
    const AutoplaySpinRules& rules) {
    if(actual.pose!=target || !piece.can_place(target,field) || piece.can_place(target.translated(1,0),field))
        return false;
    const auto metadata=Piece::metadata(piece.type());
    Rules::SpinInput input;
    input.spinChecker=metadata.spinCheck;input.pose=target;
    input.mapWidth=field.width();input.mapHeight=field.height();
    input.immobile=!piece.can_place(target.translated(-1,0),field) &&
        !piece.can_place(target.translated(0,-1),field) && !piece.can_place(target.translated(0,1),field);
    input.bonuses=rules.bonuses;input.allSpinEnabled=rules.allSpinEnabled;input.noHoldReverse=rules.noHoldReverse;
    input.rotationSystem=metadata.rotationSystem;input.canonicalGeometry=metadata.tetrioRotation;
    // Use the same occupied cells as placement resolution, including the
    // placed piece for spin checkers whose check points overlap its cells.
    const auto occupied=[&](int row,int col) {
        if(field.contains(row,col) && field.cell(row,col).occupied()) return true;
        const auto& cells=piece.cells(target.rotation);
        return std::find(cells.begin(),cells.end(),Piece::Offset{row-target.position.row,col-target.position.col})!=cells.end();
    };
    const auto classify=[&](Runtime::RotationRecord record) {
        input.lastRotation=rules.suppressNonTSpins && !Piece::t_spin_check(metadata.spinCheck)
            ? Runtime::RotationRecord::none() : record;
        return Rules::detect_spin(input,occupied);
    };
    const auto expected=classify(rotation),observed=classify(actual.lastRotation);
    return expected.spin==observed.spin && (!expected.spin || expected.mini==observed.mini);
}
std::optional<AutoplayPath> make_autoplay_path(const Runtime::Playfield& field,const Piece::Definition& piece,
    MovementState start,const Runtime::HandlingConfig& h,const Runtime::MovementRestrictions& r,
    Runtime::GravityInterval gravity,std::string_view script,bool timed) {
    if(!piece.can_place(start.pose,field)) return {};
    AutoplayPath result;PresentationState state{start};
    for(char code:script) {
        const auto command=decode_movement_script_command(code);
        if(command==MovementScriptCommand::Invalid) return {};
        auto edge=transition(state,command,field,piece,h,r,gravity,timed);
        if(edge.steps.empty()) return {};
        state=std::move(edge.state);result.script+=code;result.duration+=edge.duration;
        result.steps.insert(result.steps.end(),edge.steps.begin(),edge.steps.end());
        if(edge.terminated) {result.finalMovement=state.movement;return result;}
    }
    return {};
}
std::optional<AutoplayPath> search_autoplay_path(const Runtime::Playfield& field,const Piece::Definition& piece,
    MovementState start,Piece::Pose target,Runtime::RotationRecord rotation,const Runtime::HandlingConfig& h,
    const Runtime::MovementRestrictions& r,Runtime::GravityInterval gravity,bool timed,const std::function<bool()>& cancelled,
    const AutoplaySpinRules& spinRules) {
    if(!piece.can_place(start.pose,field) || !piece.can_place(target,field)) return {};
    if(r.disableHardDrop && !gravity.enabled()) return {};
    struct Node {PresentationState state;int parent=-1;char command=0;D cost;int count=0;};
    std::vector<Node> nodes{{{start}}};using Cost=std::pair<double,int>;
    std::map<Key,Cost> best{{key(nodes.front().state),{0,0}}};
    std::priority_queue<std::tuple<double,int,int>,std::vector<std::tuple<double,int,int>>,std::greater<>> pending;
    pending.emplace(0,0,0);
    while(!pending.empty()) {
        const auto [seconds,count,index]=pending.top();pending.pop();const auto current=nodes[index];
        if(best[key(current.state)]!=Cost{seconds,count}) continue;
        if(cancelled && cancelled()) return {};
        const auto finish=r.disableHardDrop ? MovementScriptCommand::WaitForLock : MovementScriptCommand::HardDrop;
        auto dropped=transition(current.state,finish,field,piece,h,r,gravity,timed);
        if(dropped.terminated && autoplay_landing_matches(field,piece,dropped.state.movement,target,rotation,spinRules)) {
            std::string script(1,movement_script_command_char(finish));
            for(int at=index;nodes[at].parent>=0;at=nodes[at].parent) script+=nodes[at].command;
            std::reverse(script.begin(),script.end());return make_autoplay_path(field,piece,start,h,r,gravity,script,timed);
        }
        for(char code:kMovementSearchCommands) {
            const auto command=decode_movement_script_command(code);
            if(!movement_script_command_allowed(command,r,h.das==D{},gravity.enabled(),gravity.is_instant(),
                soft_drop_interval(gravity,h.softDropFactor,h.softDropFixed)==D{})) continue;
            auto edge=transition(current.state,command,field,piece,h,r,gravity,timed);
            if(edge.steps.empty()) continue;
            const auto cost=current.cost+edge.duration;const Cost candidate{cost.seconds(),current.count+1};
            const auto identity=key(edge.state);auto prior=best.find(identity);
            if(prior!=best.end() && prior->second<=candidate) continue;
            best[identity]=candidate;const auto next=static_cast<int>(nodes.size());
            nodes.push_back({std::move(edge.state),index,code,cost,current.count+1});
            pending.emplace(candidate.first,candidate.second,next);
        }
    }
    return {};
}
}
