#include "tetr/bot/quick_play_search.h"
#include "bot_piece_access.h"
#include "tetr/engine/autoplay_path.h"
#include "tetr/engine/field_mutation.h"
#include "tetr/engine/garbage_field.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/engine/player_input.h"
#include "tetr/rules/frame_update.h"
#include "tetr/rules/garbage_generation.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/rules/quick_play.h"
#include "tetr/rules/spin.h"
#include "tetr/util/bit_ops.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <tuple>
#include <unordered_set>

namespace Bot::QuickPlaySearch {
namespace {
constexpr int hidden=Runtime::kHiddenRowCapacity;
using Cell=Runtime::BoardCell;
using Duration=Runtime::Duration;
using TimePoint=Runtime::TimePoint;

// Search copies only the actual columns, while retaining the host's complete
// hidden area and cell kinds (piece, normal garbage, timed and permanent solid).
struct Board {
    int width=10,height=20;
    std::vector<Cell> cells;
    Board(int w,int h):width(w),height(h),cells((hidden+h)*w) {}
    explicit Board(const Runtime::Playfield& field):Board(field.width(),field.height()) {
        for(int y=-hidden;y<height;++y) for(int x=0;x<width;++x) at(y,x)=field.cell(y,x);
    }
    Cell get(int y,int x) const {
        return y>=-hidden && y<height && x>=0 && x<width
            ? cells[(y+hidden)*width+x] : Cell::solid_garbage();
    }
    Cell& at(int y,int x) { return cells[(y+hidden)*width+x]; }
    void copy_row(int destination,int source) {
        for(int x=0;x<width;++x) at(destination,x)=get(source,x);
    }
    std::shared_ptr<Runtime::Playfield> materialize() const {
        auto field=std::make_shared<Runtime::Playfield>(); field->initialize(width,height);
        for(int y=-hidden;y<height;++y) for(int x=0;x<width;++x) field->set_cell(y,x,get(y,x));
        return field;
    }
    std::vector<std::uint32_t> bits() const {
        std::vector<std::uint32_t> result(height+hidden);
        for(int y=-hidden;y<height;++y) for(int x=0;x<width;++x)
            if(get(y,x).occupied()) result[height-1-y]|=std::uint32_t{1}<<x;
        return result;
    }
};
bool can_place(const Board& board,const Piece::Definition& piece,Piece::Pose pose) {
    for(const auto& cell:piece.cells(pose.rotation))
        if(board.get(pose.position.row+cell[0],pose.position.col+cell[1]).occupied())return false;
    return true;
}

Runtime::RuntimeConfig rules(const Environment& env) {
    Runtime::RuntimeConfig c=env.quickPlayRules ? *env.quickPlayRules : Runtime::RuntimeConfig{};
    c.garbage.mode=Runtime::GarbageMode::QuickPlay;
    c.field.width=env.fieldWidth;c.field.height=env.fieldHeight;
    c.field.holdEnabled=env.holdEnabled;c.field.nextCount=env.nextCount;
    c.handling.gravityInterval=!env.naturalGravityEnabled ? Runtime::GravityInterval::disabled() :
        env.gravityIntervalSeconds<=1e-7 ? Runtime::GravityInterval::instant() :
        Runtime::GravityInterval::periodic(Duration::from_seconds(env.gravityIntervalSeconds));
    c.handling.das=Duration::from_seconds(env.dasSeconds);c.handling.arr=Duration::from_seconds(env.arrSeconds);
    c.handling.softDropFactor=env.softDropFactor;c.handling.softDropFixed=env.softDropFixed;
    c.movement=env.movement;c.spinBonuses=env.spinBonuses;c.attackRules=env.attackRules;
    c.legacySpinScoring=env.legacySpinScoring;
    c.garbage.cap=env.garbageCap;c.garbage.sendMultiplier=env.sendMultiplier;
    c.garbage.hitMultiplier=env.hitMultiplier;c.garbage.cancelMultiplier=env.cancellationMultiplier;
    if(!env.quickPlayRules) {
        c.quickPlay.expert=static_cast<Runtime::ModifierMode>(env.quickPlayExpert);
        c.quickPlay.noHold=static_cast<Runtime::ModifierMode>(env.quickPlayNoHold);
        c.quickPlay.messy=static_cast<Runtime::ModifierMode>(env.quickPlayMessy);
        c.quickPlay.gravity=static_cast<Runtime::ModifierMode>(env.quickPlayGravity);
        c.quickPlay.volatileMode=static_cast<Runtime::ModifierMode>(env.quickPlayVolatile);
        c.quickPlay.doubleHole=static_cast<Runtime::ModifierMode>(env.quickPlayDoubleHole);
        c.quickPlay.allSpin=static_cast<Runtime::AllSpinMode>(env.quickPlayAllSpin);
        c.quickPlay.backfire=static_cast<Runtime::ModifierMode>(env.quickPlayBackfire);
        c.quickPlay.snakeEyes=static_cast<Runtime::ModifierMode>(env.quickPlaySnakeEyes);
        c.quickPlay.snowball=static_cast<Runtime::ModifierMode>(env.quickPlaySnowball);
    }
    return c;
}

struct State {
    Board board;
    Engine::PlacementApplicationState status;
    Runtime::GarbageSessionState garbage;
    int activeHeight=20,solidHeight=0,garbageHeight=0;
    int floor=1;
    bool previousClear=false,dead=false,immediateDeath=false;
    double clock=0,elapsed=0,gravityUpdate=0;
    Runtime::GravityInterval gravity=Runtime::GravityInterval::disabled();
    int index=0,hold=-1,root=-1,depth=0,attack=0,outgoing=0,cancelled=0,risen=0;
    Piece::Pose rootPose{};
    Runtime::RotationRecord rootRotation{};
    double reward=0,pathRisk=0,value=-1e100;
    explicit State(const Runtime::Playfield& field):board(field) {}
};

State initial(const Environment& env,const Runtime::PlayerSessionState& player,const Runtime::Playfield& field) {
    State s(field);
    const auto& p=player.gameplay; const auto& score=p.scoring;
    s.activeHeight=p.playfield.activeHeight;s.solidHeight=p.playfield.solidGarbageHeight;
    s.garbageHeight=p.playfield.garbageHeight;s.previousClear=player.clutch.previousPieceCleared;
    s.clock=env.nowSeconds;s.elapsed=env.elapsedGameSeconds;s.gravityUpdate=player.timing.qpGravity.seconds();
    s.floor=env.quickPlayFloor;
    s.gravity=rules(env).handling.gravityInterval;
    auto& a=s.status;
    a.placedBlocks=score.placedBlocks;a.totalAttack=score.totalAttack;a.totalOutgoingAttack=score.totalOutgoingAttack;
    a.totalLines=score.totalLines;a.totalGarbageLines=score.totalGarbageLines;a.score=score.score;
    a.difficultClears=score.difficultClears;a.b2bExtensions=score.b2bExtensions;a.b2bBreaks=score.b2bBreaks;
    a.maxB2B=score.maxB2B;a.maxCombo=score.maxCombo;a.clearMoves=score.clearMoves;a.zeroAttackClears=score.zeroAttackClears;
    a.attacksAtLeast4=score.attacksAtLeast4;a.attacksAtLeast8=score.attacksAtLeast8;a.maxAttack=score.maxAttack;
    a.b2b=score.b2b;a.combo=score.combo;a.qpBlighted=p.quickPlay.blighted;
    a.qpLastOperation=p.quickPlay.lastOperation;a.qpOperations=p.quickPlay.operations;
    a.qpProgress=env.quickPlayProgress;a.qpAltitude=env.quickPlayAltitude;a.qpSpeed=env.quickPlayBooster;
    s.garbage.buffer=player.garbage.buffer;s.garbage.cancellation=player.garbage.cancellation;
    s.garbage.rising=player.garbage.rising;s.garbage.incomingFraction=player.garbage.incomingFraction;
    s.garbage.holePattern=player.garbage.holePattern;
    s.garbage.previousHoleColumn=player.garbage.previousHoleColumn;
    s.garbage.chessboardParity=player.garbage.chessboardParity;
    s.garbage.holeOrder.resize(field.width());
    std::iota(s.garbage.holeOrder.begin(),s.garbage.holeOrder.end(),0);
    // Independent common samples, never the live RNG or its pre-shuffled holes.
    s.garbage.random.seed(0x715EEDu);
    return s;
}

Rules::GarbageGenerationConfig generation(const State& s,const Runtime::RuntimeConfig& c) {
    Rules::GarbageGenerationConfig g;
    g.width=s.board.width;g.garbageMode=Runtime::GarbageMode::QuickPlay;
    g.cheeseModel=c.garbage.cheeseModel;g.cheeseMessiness=c.garbage.cheeseMessiness;
    g.doubleHoleMode=c.quickPlay.doubleHole;g.reverseNoHold=Runtime::reverse(c.quickPlay.noHold);
    g.legacyShapeRules=c.garbage.legacyShapeRules;g.avoidSameColumn=c.garbage.avoidSameColumn;
    g.qpPolicy=true;g.singleMessiness=Rules::qp_messiness(c.quickPlay,s.floor,s.status.qpProgress,s.elapsed);
    g.continuousMessiness=2.5*g.singleMessiness;
    g.favor=Rules::qp_garbage_favor(c.quickPlay,s.floor);
    g.concentrate=Runtime::reverse(c.quickPlay.volatileMode) ||
        (s.floor<=5 && g.continuousMessiness<=.15);
    int top=s.activeHeight;std::vector<int> holes;
    for(int y=-hidden;y<s.activeHeight;++y) {
        bool grey=false;
        for(int x=0;x<s.board.width;++x) {
            if(s.board.get(y,x).occupied())top=std::min(top,y);
            grey|=s.board.get(y,x).is_normal_garbage();
        }
        if(grey && holes.empty())for(int x=0;x<s.board.width;++x)
            if(s.board.get(y,x).empty_cell())holes.push_back(x);
    }
    for(int x=0;x<s.board.width;++x) {
        int lowest=top,distance=holes.empty()?0:s.board.width;bool empty=false;
        for(int y=s.activeHeight-1;y>=top;--y) if(s.board.get(y,x).empty_cell()) {lowest=y;empty=true;break;}
        for(int h:holes)distance=std::min(distance,std::abs(h-x));
        g.digScores.push_back(empty ? lowest-top+5*distance : 0);
    }
    return g;
}

Rules::GarbageGenerationState hole_state(const State& s) {
    return {s.garbage.holeOrder,s.garbage.holePattern,s.garbage.chessboardParity,s.garbage.previousHoleColumn};
}
void store_holes(State& s,const Rules::GarbageGenerationState& g) {
    s.garbage.holeOrder=g.holeOrder;s.garbage.chessboardParity=g.chessboardParity;s.garbage.previousHoleColumn=g.lastColumn;
}
void shift(State& s,int bottom,int lines) {
    Engine::shift_field_up_for_garbage(s.board.width,-hidden,bottom,lines,
        [&](int y,int x){return s.board.get(y,x);},[&](int d,int from){s.board.copy_row(d,from);},false);
}
void add_rows(State& s,const Runtime::RuntimeConfig& c,int lines,bool preserve=false) {
    lines=std::clamp(lines,0,s.activeHeight+hidden);if(!lines)return;
    auto g=generation(s,c);auto holes=hole_state(s);
    Rules::prepare_garbage_batch(g,holes,s.garbage.random,preserve);
    shift(s,s.activeHeight,lines);
    for(int y=s.activeHeight-lines;y<s.activeHeight;++y) {
        const auto row=Rules::generate_garbage_row(g,holes,s.garbage.random);
        for(int x=0;x<s.board.width;++x)s.board.at(y,x)=row[x];
    }
    s.garbageHeight+=lines;store_holes(s,holes);
    if(c.quickPlay.continuousCancellation)Rules::qp_relieve_cancellation(s.status.qpProgress,lines*3);
}
void add_solid(State& s,const Runtime::RuntimeConfig& c,int lines,bool timed) {
    lines=std::clamp(lines,0,s.activeHeight);if(!lines)return;
    const int bottom=s.board.height-(timed?s.solidHeight:0);
    auto holes=hole_state(s);shift(s,bottom,lines);
    for(int y=bottom-lines;y<bottom;++y) {
        const auto row=Rules::generate_solid_garbage_row(s.board.width,
            timed?Cell::timed_solid():Cell::solid_garbage(),c.quickPlay.doubleHole,
            5+s.floor,holes,s.garbage.random);
        for(int x=0;x<s.board.width;++x)s.board.at(y,x)=row[x];
    }
    s.activeHeight-=lines;if(!timed)s.solidHeight+=lines;store_holes(s,holes);
}
double multiplier(const Runtime::RuntimeConfig& c) {return c.timeMultiplier>0?c.timeMultiplier:1;}
Duration delay(const State& s,const Runtime::RuntimeConfig& c) {
    return Rules::qp_garbage_delay(c.quickPlay,s.floor,s.status.qpProgress.cancellation)/multiplier(c);
}
void receive(State& s,const Runtime::RuntimeConfig& c,int attack,bool resolved=false) {
    int lines=attack;
    if(!resolved) {
        const double adjusted=Rules::qp_receive_attack(attack,s.status.qpAltitude,s.status.qpAltitude,
            s.floor,c.quickPlay,s.status.qpProgress,s.elapsed);
        const double exact=std::floor(adjusted)*std::clamp(c.garbage.hitMultiplier,0.0,1000.0)+
            (c.garbage.legacyIncomingRounding?0:s.garbage.incomingFraction);
        lines=static_cast<int>(std::min(std::floor(exact+(c.garbage.legacyIncomingRounding?0:1e-9)),double(std::numeric_limits<int>::max())));
        s.garbage.incomingFraction=c.garbage.legacyIncomingRounding?0:std::max(0.0,exact-lines);
    }
    if(lines<=0)return;
    auto& p=s.status.qpProgress;
    p.receiveProtection=std::min(18,p.receiveProtection+(Runtime::reverse(c.quickPlay.volatileMode)?lines/3:lines));p.sinceReceived=0;
    s.garbage.buffer.push_back(Rules::make_garbage_packet(lines,1,TimePoint::from_seconds(s.clock),false,delay(s,c)));
}
void arm(State& s,const Runtime::RuntimeConfig& c) {
    if(s.garbage.rising.remainingLines>0 || c.garbage.cap<=0)return;
    const auto now=TimePoint::from_seconds(s.clock);
    const auto interval=Rules::qp_garbage_rise_interval(c.quickPlay,c.garbage.riseInterval)/multiplier(c);
    if(interval<=Duration{}) {
        const auto plan=Rules::consume_ready_garbage(s.garbage.buffer,now,delay(s,c),c.garbage.cap);
        for(const auto& row:plan.chunks) {add_rows(s,c,row.lines,row.preserveHole);s.risen+=row.lines;}
    } else {
        int left=std::min(c.garbage.cap,s.activeHeight+hidden),count=0;
        for(const auto& p:s.garbage.buffer) {
            if(now-p.arrival_time()<delay(s,c) || left<=0)break;
            const int take=std::min(left,p.line_count());left-=take;count+=take;
        }
        if(count) {
            auto& r=s.garbage.rising;r.remainingLines=count;r.interval=interval;
            r.nextRow=std::max(now+interval,r.nextRow.value_or(TimePoint{}));
        }
    }
    if(s.garbage.buffer.empty())s.garbage.cancellation.cancellationFraction=0;
}
void advance_rows(State& s,const Runtime::RuntimeConfig& c) {
    auto& r=s.garbage.rising;
    const auto now=TimePoint::from_seconds(s.clock);
    while(r.remainingLines>0 && r.nextRow && *r.nextRow<=now) {
        const auto plan=Rules::consume_ready_garbage(s.garbage.buffer,now,{},1);
        if(plan.chunks.empty()){r.remainingLines=0;break;}
        add_rows(s,c,1,plan.chunks.front().preserveHole);++s.risen;--r.remainingLines;*r.nextRow+=r.interval;
    }
    if(s.garbage.buffer.empty()){r.remainingLines=0;s.garbage.cancellation.cancellationFraction=0;}
}
void advance_time(State& s,const Runtime::RuntimeConfig& c,double seconds) {
    for(double left=std::isfinite(seconds)?std::max(0.0,seconds):0;left>1e-9;) {
        double dt=std::min(.05,left);
        if(s.gravityUpdate+1>s.clock)dt=std::min(dt,s.gravityUpdate+1-s.clock);
        if(s.garbage.rising.remainingLines>0 && s.garbage.rising.nextRow && s.garbage.rising.nextRow->seconds()>s.clock)
            dt=std::min(dt,s.garbage.rising.nextRow->seconds()-s.clock);
        left-=dt;s.clock+=dt;s.elapsed+=dt;
        Rules::FrameRuleState frame;
        frame.qpSpeed=s.status.qpSpeed;frame.qpAltitude=s.status.qpAltitude;frame.qpFloor=s.floor;
        frame.qpProgress=s.status.qpProgress;frame.gravitySpeed=s.gravity;
        Rules::FrameRuleConfig config;
        config.garbageMode=Runtime::GarbageMode::QuickPlay;config.generateOpponentPressure=false;
        config.qpMods=c.quickPlay;config.qpModExpert=c.quickPlay.expert;config.qpModGravity=c.quickPlay.gravity;
        config.elapsedGameTime=Duration::from_seconds(s.elapsed);
        const auto result=Rules::advance_frame_before_presentation(frame,config,Duration::from_seconds(dt),s.garbage.random);
        Rules::advance_frame_after_presentation(frame,config,Duration::from_seconds(dt));
        s.status.qpSpeed=frame.qpSpeed;s.status.qpAltitude=frame.qpAltitude;s.status.qpProgress=std::move(frame.qpProgress);
        s.gravity=frame.gravitySpeed;s.floor=frame.qpFloor;
        for(int attack:result.resolvedQpGarbagePackets)receive(s,c,attack,true);
        add_solid(s,c,result.permanentGarbageLines,false);
        if(c.garbage.entryMode==Runtime::GarbageEntryMode::Immediate)arm(s,c);
        advance_rows(s,c);
        if(s.clock+1e-9>=s.gravityUpdate+1) {s.gravity=Engine::advance_qp_gravity_speed(s.gravity,true);s.gravityUpdate+=1;}
    }
}

void grow(State& s,const Runtime::RuntimeConfig& c) {
    if(!Runtime::enabled(c.quickPlay.snowball))return;
    const int size=Rules::qp_snowball_size(s.status.totalLines);
    if(size<=s.board.width)return;
    const int oldWidth=s.board.width;
    const int offset=(size-4)/2-(oldWidth-4)/2,dh=size-s.board.height;
    Board bigger(size,size);auto g=generation(s,c);g.width=size;
    for(int y=-hidden+dh;y<size;++y) {
        std::vector<Cell> old;
        for(int x=0;x<s.board.width;++x)old.push_back(s.board.get(y-dh,x));
        const auto row=Rules::expand_garbage_row(g,old,offset,s.garbage.random);
        for(int x=0;x<size;++x)bigger.at(y,x)=row[x];
    }
    s.board=std::move(bigger);s.activeHeight+=dh;
    for(int& x:s.garbage.holeOrder)x+=offset;
    for(int x=0;x<size;++x)if(x<offset || x>=offset+oldWidth) {
        if(std::find(s.garbage.holeOrder.begin(),s.garbage.holeOrder.end(),x)==s.garbage.holeOrder.end())s.garbage.holeOrder.push_back(x);
    }
    if(s.garbage.previousHoleColumn>=0)s.garbage.previousHoleColumn+=offset;
}

struct Projection {Rules::PlacementResult placement;Engine::PlacementApplicationResult application;int cancelled=0;};
Projection place(State& s,const Runtime::RuntimeConfig& c,Piece::Type type,Piece::Pose pose,
    Runtime::RotationRecord rotation,double pieceTick=0) {
    const auto piece=Piece::get_definition(type);
    Rules::PlacementInput in;
    in.mapWidth=s.board.width;in.mapHeight=s.activeHeight;in.firstRow=-hidden;
    in.garbageStartRow=s.activeHeight-s.garbageHeight;in.layerStartRow=s.activeHeight-c.garbage.layerHeight;
    in.pose=pose;in.lastRotation=rotation;in.currentB2B=s.status.b2b;in.currentCombo=s.status.combo;
    in.garbageMode=Runtime::GarbageMode::QuickPlay;in.attackRoundingRoll=1;
    in.allSpinMod=c.quickPlay.allSpin;in.noHoldMod=c.quickPlay.noHold;
    const auto metadata=Piece::metadata(type);
    in.spinChecker=metadata.spinCheck;in.canonicalGeometry=metadata.tetrioRotation;
    in.piece={metadata.familyIndex,metadata.rotationSystem};in.spinBonuses=c.spinBonuses;
    in.attackRules=c.attackRules;in.garbageMultiplier=c.garbage.sendMultiplier;in.legacySpinScoring=c.legacySpinScoring;
    in.grounded=!can_place(s.board,piece,pose.translated(1,0));
    in.immobile=in.grounded && !can_place(s.board,piece,pose.translated(-1,0)) &&
        !can_place(s.board,piece,pose.translated(0,-1)) && !can_place(s.board,piece,pose.translated(0,1));
    if(Runtime::reverse(c.quickPlay.doubleHole) && !Piece::t_spin_check(metadata.spinCheck))in.lastRotation={};
    in.qp={c.quickPlay.expert,c.quickPlay.noHold,c.quickPlay.messy,c.quickPlay.gravity,c.quickPlay.volatileMode,
        c.quickPlay.doubleHole,c.quickPlay.invisible,c.quickPlay.allSpin,c.quickPlay.backfire,c.quickPlay.snakeEyes,c.quickPlay.snowball};
    for(const auto& cell:piece.cells(pose.rotation))s.board.at(pose.position.row+cell[0],pose.position.col+cell[1])=Cell::piece(piece.color_id());
    Projection out;
    out.placement=Rules::resolve_placement(in,[&](int y,int x){return s.board.get(y,x);},
        [&](int y,int x){return s.board.get(y,x).occupied();});
    auto& p=out.placement;s.garbageHeight-=p.clear.clearedGarbageLines;
    Engine::PlacementApplicationConfig app;
    app.garbageMode=Runtime::GarbageMode::QuickPlay;app.qpMods=c.quickPlay;
    app.qpExpert=c.quickPlay.expert;app.qpVolatile=c.quickPlay.volatileMode;app.qpDoubleHole=c.quickPlay.doubleHole;
    app.qpAllSpin=c.quickPlay.allSpin;app.qpBackfire=c.quickPlay.backfire;app.qpSnakeEyes=c.quickPlay.snakeEyes;
    app.pieceFamily=metadata.familyIndex;app.pieceColumn=pose.position.col;
    if(metadata.familyIndex==0 && p.clear.clearedLines==0 && piece.cells(pose.rotation).size()==4) {
        const int column=pose.position.col+piece.cells(pose.rotation).front()[1];
        if(column<=1 || column>=s.board.width-2) {
            app.stackedEdgeI=true;const int neighbor=column<=1?1-column:2*s.board.width-3-column;
            for(const auto& cell:piece.cells(pose.rotation)) {
                const auto adjacent=s.board.get(pose.position.row+cell[0],neighbor);
                app.stackedEdgeI&=pose.position.col+cell[1]==column && adjacent.is_piece() && adjacent.color_id()==piece.color_id();
            }
        }
    }
    auto begin=Engine::begin_placement_application(s.status,app,p,type);
    receive(s,c,begin.effects.preOffsetBufferLines);
    int raw=0;
    if(begin.effects.offsetClearTrigger>0) {
        const int before=Rules::pending_garbage(s.garbage.buffer);
        const auto cancel=Rules::cancel_incoming_garbage(s.garbage.buffer,
            {begin.effects.offsetAttack,begin.state.placedBlocks,false,c.garbage.openerPhase,
             s.status.totalOutgoingAttack,c.garbage.legacyOpenerPhase,0,c.garbage.cancelMultiplier},&s.garbage.cancellation);
        raw=cancel.outgoingAttack;out.cancelled=before-Rules::pending_garbage(s.garbage.buffer);
        s.garbage.rising.remainingLines=std::max(0,s.garbage.rising.remainingLines-out.cancelled);
        const auto next=TimePoint::from_seconds(s.clock)+Duration::from_seconds(.2)/multiplier(c);
        s.garbage.rising.nextRow=std::max(next,s.garbage.rising.nextRow.value_or(TimePoint{}));
    } else if(c.garbage.entryMode==Runtime::GarbageEntryMode::AfterNonClear)arm(s,c);
    begin.state.qpProgress=s.status.qpProgress;
    out.application=Engine::finish_placement_application(begin,app,raw);
    s.status=out.application.state;
    if(out.application.effects.decayTimedSolidGarbage) {
        const auto decay=Engine::decay_timed_solid_garbage(s.board.width,-hidden,s.board.height,
            [&](int y,int x){return s.board.get(y,x);},[&](int y,int x,Cell value){s.board.at(y,x)=value;});
        s.activeHeight+=decay.releasedRows;
    }
    receive(s,c,out.application.effects.postOffsetBufferLines);
    const auto moves=Rules::build_gravity_collapse_plan(-hidden,s.activeHeight,p.clear.clearedRows);
    Engine::apply_gravity_collapse(s.board.width,moves,[&](int y,int x){return s.board.get(y,x);},
        [&](int d,int from){s.board.copy_row(d,from);});
    grow(s,c);
    // Piece time settles after clear/growth, before rDH replenishment and AS
    // penalties. This matters when the tick also crosses a fatigue boundary.
    if(pieceTick>0)advance_time(s,c,pieceTick);
    if(Runtime::reverse(c.quickPlay.doubleHole))add_rows(s,c,p.clear.clearedLayerLines);
    add_solid(s,c,out.application.effects.solidGarbageLines,true);
    if(c.quickPlay.allSpin==Runtime::AllSpinMode::Reverse && out.application.effects.solidGarbageLines>=20)
        s.dead=s.immediateDeath=true;
    if(c.garbage.entryMode!=Runtime::GarbageEntryMode::AfterNonClear)arm(s,c);
    s.previousClear=p.clear.clearedLines>0;s.dead|=s.activeHeight<=0;
    s.attack+=begin.effects.attack;s.outgoing+=out.application.effects.outgoingAttack;s.cancelled+=out.cancelled;++s.depth;
    return out;
}

int roof(const Board& board) {
    for(int y=-hidden;y<board.height;++y)for(int x=0;x<board.width;++x)
        if(board.get(y,x).occupied())return board.height-y;
    return 0;
}

// ZZZ Botris v0.8's terrain terms, lifted out of its fixed tetromino graph.
// Removing the empty-board constant permits comparison across SB growth.
double botris_shape(const Board& board) {
    const auto rows=board.bits();const int top=roof(board),w=board.width;
    const std::uint32_t full=w==32?~std::uint32_t{}:(std::uint32_t{1}<<w)-1;
    int columns=-2*top,transitions=top==0?0:-w,holes=0,holeDepth=0,wellDepth=0;
    std::vector<int> depths(w),wells(w),covers;
    for(int y=0;y<top;++y) {
        columns+=!(rows[y]&1);columns+=!(rows[y]&(std::uint32_t{1}<<(w-1)));
        columns+=Util::popcount32((rows[y]^(rows[y]<<1))&(full&~std::uint32_t{1}));
        if(y)transitions+=Util::popcount32(rows[y-1]^rows[y]);
    }
    transitions+=Util::popcount32(full&~rows[0]);
    if(top)transitions+=Util::popcount32(rows[top-1]);
    if(!top)transitions-=w;
    std::uint32_t covered=0;
    for(int y=top-1;y>=0;--y) {
        covered|=rows[y];const auto missing=covered^rows[y];
        if(missing) {
            ++holes;int cover=0;
            for(int above=y+1;above<top;++above) {
                const auto bits=missing&rows[above];if(!bits)break;
                cover+=(above+1)*Util::popcount32(bits);
            }
            covers.push_back(cover);
        }
        for(int x=0;x<w;++x) {
            if(missing&(std::uint32_t{1}<<x))holeDepth+=++depths[x];else depths[x]=0;
            const bool well=!(covered&(std::uint32_t{1}<<x)) &&
                (x==0 || covered&(std::uint32_t{1}<<(x-1))) &&
                (x==w-1 || covered&(std::uint32_t{1}<<(x+1)));
            if(well)wellDepth+=++wells[x];
        }
    }
    double value=-top*128-columns*160-transitions*160-holes*380-wellDepth*100-holeDepth*40;
    double rate=32;for(int cover:covers){value-=cover*rate;rate*=.25;}
    return value;
}

std::optional<Piece::Pose> spawn(const State& s,Piece::Type type,const Runtime::RuntimeConfig& c) {
    if(s.activeHeight<=0)return {};
    const auto piece=Piece::get_definition(type);
    auto pose=Piece::spawn_pose(s.board.width,type);const auto& cells=piece.cells(pose.rotation);
    int lo=cells.front()[1],hi=lo,top=cells.front()[0];
    for(const auto& cell:cells){lo=std::min(lo,cell[1]);hi=std::max(hi,cell[1]);top=std::min(top,cell[0]);}
    if(Runtime::enabled(c.quickPlay.snowball) && hi-lo+1<=s.board.width)
        pose=Piece::fit_spawn_inside_walls(pose,cells,s.board.width);
    if(can_place(s.board,piece,pose))return pose;
    if(s.previousClear && pose.position.col+lo>=0 && pose.position.col+hi<s.board.width)
        while(--pose.position.row>=-hidden-top)if(can_place(s.board,piece,pose))return pose;
    return {};
}

struct Landing {Piece::Pose pose;Runtime::RotationRecord rotation;};
std::uint64_t identity(const Engine::MovementState& movement) {
    return (std::uint64_t(movement.pose.position.row+256)<<24) |
        (std::uint64_t(movement.pose.position.col+128)<<12) |
        (std::uint64_t(movement.pose.rotation.index())<<8) |
        // Only rotation/no rotation and T's fifth kick change spin rewards.
        // Keep the original record in the node for execution and replay.
        (movement.lastRotation.rotated()?(movement.lastRotation.kick().index()==4?2:1):0);
}
std::vector<Landing> landings(const State& s,const Runtime::Playfield& field,Piece::Type type,
    Piece::Pose start,const Runtime::RuntimeConfig& c,const Environment& env) {
    std::vector<Landing> result;
    const auto piece=Piece::get_definition(type);
    const auto metadata=Piece::metadata(type);
    const auto canPlace=[&](Piece::Pose pose){return piece.can_place(pose,field);};
    const auto tryRotate=[&](Piece::Pose& pose,Runtime::Rotation r,Piece::KickIndex& kick){return piece.try_rotate(pose,r,field,kick);};
    if(!canPlace(start))return result;
    Engine::MovementState root;root.pose=start;
    const bool instant=s.gravity.is_instant(),soft=Engine::soft_drop_interval(s.gravity,c.handling.softDropFactor,c.handling.softDropFixed)==Duration{};
    if(instant)Engine::apply_presentation_instant_gravity(root,canPlace);
    std::queue<Engine::MovementState> queue;queue.push(root);
    std::unordered_set<std::uint64_t> visited{identity(root)};
    std::unordered_set<std::uint64_t> emitted;
    const auto emit=[&](Engine::MovementState movement) {
        if(!c.movement.disableHardDrop)
            Engine::apply_movement_script_command(movement,Engine::MovementScriptCommand::HardDrop,canPlace,tryRotate);
        if(canPlace(movement.pose.translated(1,0)))return;
        if(c.movement.disableHardDrop && s.gravity.is_disabled())return;
        Rules::SpinInput spin;
        spin.spinChecker=metadata.spinCheck;spin.pose=movement.pose;
        spin.lastRotation=Runtime::reverse(c.quickPlay.doubleHole) && !Piece::t_spin_check(metadata.spinCheck)
            ? Runtime::RotationRecord{} : movement.lastRotation;
        spin.mapWidth=s.board.width;spin.mapHeight=s.activeHeight;
        spin.immobile=!canPlace(movement.pose.translated(-1,0)) &&
            !canPlace(movement.pose.translated(0,-1)) && !canPlace(movement.pose.translated(0,1));
        spin.bonuses=c.spinBonuses;spin.allSpinEnabled=Runtime::enabled(c.quickPlay.allSpin);
        spin.noHoldReverse=Runtime::reverse(c.quickPlay.noHold);
        spin.rotationSystem=metadata.rotationSystem;spin.canonicalGeometry=metadata.tetrioRotation;
        const auto classification=Rules::detect_spin(spin,[&](int y,int x) {
            if(s.board.get(y,x).occupied())return true;
            const auto& cells=piece.cells(movement.pose.rotation);
            return std::find(cells.begin(),cells.end(),Piece::Offset{
                y-movement.pose.position.row,x-movement.pose.position.col})!=cells.end();
        });
        const auto key=(identity(movement)&~std::uint64_t{255}) |
            (classification.spin?(classification.mini?1:2):0);
        if(emitted.insert(key).second)result.push_back({movement.pose,movement.lastRotation});
    };
    while(!queue.empty() && visited.size()<50000) {
        if(env.searchCancelled && env.searchCancelled->load())break;
        const auto current=queue.front();queue.pop();emit(current);
        for(char code:Engine::kMovementSearchCommands) {
            const auto command=Engine::decode_movement_script_command(code);
            if(!Engine::movement_script_command_allowed(command,c.movement,env.wall_only_horizontal(),s.gravity.enabled(),instant,soft))continue;
            auto next=current;
            const auto effect=Engine::apply_movement_script_command(next,command,canPlace,tryRotate,
                env.wall_only_horizontal(),env.arrSeconds==0,c.movement,instant,soft);
            if(effect.changed && visited.insert(identity(next)).second)queue.push(next);
        }
    }
    return result;
}

double danger(const State& s,const Runtime::RuntimeConfig& c,double duration) {
    int announced=0;for(const auto& windup:s.status.qpProgress.windups)
        if(windup.due<=s.elapsed+duration)announced+=windup.attack;
    const double received=Rules::qp_receive_attack(1,s.status.qpAltitude,s.status.qpAltitude,
        s.floor,c.quickPlay,s.status.qpProgress,s.elapsed)*c.garbage.hitMultiplier;
    const double uncertainty=Rules::qp_survival_apm(s.status.qpAltitude)*duration/60*received;
    return std::max(0.0,roof(s.board)+std::min(c.garbage.cap,
        Rules::pending_garbage(s.garbage.buffer)+announced)+uncertainty-s.board.height*.6);
}

// A reachable, uncovered one-column well is useful attack preparation. The
// terrain score otherwise penalizes its depth quadratically, so a short
// search repeatedly fills it for a single instead of waiting for a quad.
double attack_setup(const State& s,Profile profile,bool legacyShape) {
    const int w=s.board.width;
    const auto full=w==32?~std::uint32_t{}:(std::uint32_t{1}<<w)-1;
    std::vector<int> top(w,s.activeHeight);
    for(int y=-hidden;y<s.activeHeight;++y)for(int x=0;x<w;++x)
        if(s.board.get(y,x).occupied())top[x]=std::min(top[x],y);
    double best=0;
    for(int x=0;x<w;++x) {
        const auto mask=full^(std::uint32_t{1}<<x);
        int rows=0,completed=0;double readiness=0;
        // The well can rest on existing garbage or pieces; it need not run
        // all the way to the original empty floor.
        for(int y=top[x]-1;y>=-hidden && rows<4;--y) {
            std::uint32_t filled=0;
            for(int col=0;col<w;++col)if(s.board.get(y,col).occupied())filled|=std::uint32_t{1}<<col;
            const int count=Util::popcount32(filled);
            if(count<std::max(1,w-3))break;
            bool fillable=true;
            for(int col=0;col<w;++col)if(!(filled&(std::uint32_t{1}<<col)) && top[col]<y){fillable=false;break;}
            if(!fillable)break;
            if(filled==mask && completed==rows)++completed;
            readiness+=double(count)/(w-1);
            ++rows;
        }
        if(!rows)continue;
        const double preparation=(profile==Profile::While1?640:720)*readiness;
        const double depthCorrection=legacyShape?0:100.0*completed*(completed+1)/2;
        best=std::max(best,preparation+depthCorrection);
    }
    return best;
}

void evaluate(State& s,const State& previous,const Projection& p,Profile profile,
    const Runtime::RuntimeConfig& c,const ShapeEvaluator& custom,double pace) {
    const double discount=std::pow(.96,s.depth-1);
    const int lines=p.placement.clear.clearedLines,dug=p.placement.clear.clearedGarbageLines;
    const double attackWeight=profile==Profile::Botris ? 128*(3+1.0/s.depth):profile==Profile::While1?512:560;
    const auto reserve=[](const Engine::PlacementApplicationState& a) {
        return 2*a.qpSpeed*(a.qpSpeed-1)+a.qpProgress.experience;
    };
    const double experience=reserve(s.status)-reserve(previous.status);
    const int spike=p.application.effects.spikeAttack;
    const double burst=(profile==Profile::While1?96:profile==Profile::Botris?64:80)*
        double(spike)*std::clamp(spike-1,0,5);
    // Normal QP rewards scattered singles with both +1 attack and clear XP.
    // Crediting XP like attack made repeated singles beat preparing a quad.
    s.reward+=discount*(attackWeight*p.application.effects.spikeAttack+
        burst+(profile==Profile::While1?512:240)*dug-40*lines+40*experience);
    const double pressure=danger(s,c,pace);
    s.pathRisk+=discount*35*pressure*pressure;
    double shape=botris_shape(s.board);
    const bool legacyShape=profile==Profile::While1 && custom && s.board.width==10 && s.board.height<=24;
    if(legacyShape)
        shape=custom(s.board.bits(),s.board.width,s.board.height,s.garbageHeight,s.status.b2b);
    s.value=shape+attack_setup(s,profile,legacyShape)+s.reward+(profile==Profile::While1?0:profile==Profile::Botris?514:480)*s.status.b2b+
        (s.status.b2b?300:0)-95*pressure*pressure-s.pathRisk;
    if(profile==Profile::Botris)s.value+=std::max(0,s.status.combo)*(s.status.combo-1)*40;
    if(s.dead)s.value=(s.immediateDeath?-1e15:-1e12)+std::clamp(s.reward,-1e6,1e6);
}

Decision choose(Profile profile,const Environment& env,const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,const std::vector<int>& supplied,const std::vector<int>& unseen,
    Piece::Pose start,int hold,const ShapeEvaluator& shape) {
    Decision answer;
    if(supplied.empty() || !availability(env))return answer;
    const auto started=std::chrono::steady_clock::now();
    const int budget=std::clamp((env.autoplaySearchWidth+5)/6,5,250);
    const auto deadline=started+std::chrono::milliseconds(budget);
    const int width=std::clamp((env.autoplaySearchWidth+11)/12,2,80);
    const int depthLimit=profile==Profile::While1?5:4;
    const double pace=env.secondsPerPiece>0?env.secondsPerPiece:env.autoplayPps>0?1/env.autoplayPps:.1;
    auto config=rules(env);
    auto next=supplied;
    // A singleton of the visible bag complement is inferable, unlike the
    // actual hidden queue. rNH and other randomizers cannot use this inference.
    if(!env.randomPieces && !config.quickPlay.continuousCancellation &&
        config.quickPlay.fool==Runtime::FoolMode::Disabled && unseen.size()==1 && unseen.front()>=0 && unseen.front()<7)
        next.push_back(unseen.front());
    std::vector<State> beam;
    auto root=initial(env,player,field);root.hold=hold>=0?hold:-1;beam.push_back(root);
    if(hold==-2 && env.holdEnabled && player.gameplay.hold.piece.valid()) {
        root.hold=player.gameplay.hold.piece.index();
        for(int family=0;family<7;++family)if(env.pieceTypes[family]==player.gameplay.hold.piece)root.hold=family;
        beam.front().hold=root.hold;
    }
    State best=root;bool haveBest=false;
    for(int depth=0;depth<depthLimit && !beam.empty();++depth) {
        std::vector<State> children;
        for(const auto& parent:beam) {
            if(parent.dead || parent.index>=static_cast<int>(next.size()))continue;
            if(depth && std::chrono::steady_clock::now()>=deadline)break;
            if(env.searchCancelled && env.searchCancelled->load())break;
            const auto image=parent.board.materialize();
            const int current=next[parent.index];
            for(bool useHold:{false,true}) {
                const bool canHold=env.holdEnabled && !Runtime::enabled(config.quickPlay.noHold) &&
                    !(depth==0 && hold==-2);
                if(useHold && (!canHold || (parent.hold<0 && parent.index+1>=static_cast<int>(next.size()))))continue;
                const int selected=useHold?(parent.hold>=0?parent.hold:next[parent.index+1]):current;
                if(useHold && selected==current)continue;
                if(selected<0)continue;
                const auto piece=BotPiece::definition(selected,env);
                const auto origin=depth==0 && !useHold ? std::optional<Piece::Pose>{start} : spawn(parent,piece.type(),config);
                if(!origin)continue;
                const auto targets=landings(parent,*image,piece.type(),*origin,config,env);
                for(const auto& landing:targets) {
                    State candidate=parent;
                    candidate.index+=1+(useHold && parent.hold<0);
                    if(useHold)candidate.hold=current;
                    if(depth==0) {candidate.root=selected;candidate.rootPose=landing.pose;candidate.rootRotation=landing.rotation;}
                    const auto projection=place(candidate,config,piece.type(),landing.pose,landing.rotation,env.pieceTime?pace:0);
                    if(!env.pieceTime)advance_time(candidate,config,std::max(pace,
                        env.entryDelaySeconds+(projection.placement.clear.clearedLines>0?env.lineClearDelaySeconds:0)));
                    if(candidate.index<static_cast<int>(next.size()) && !spawn(candidate,BotPiece::definition(next[candidate.index],env).type(),config))
                        candidate.dead=true;
                    evaluate(candidate,parent,projection,profile,config,shape,pace);
                    if(depth==0)answer.candidates.push_back({piece.type(),landing.pose,landing.rotation,candidate.value,0});
                    children.push_back(std::move(candidate));
                }
            }
        }
        if(children.empty())break;
        std::stable_sort(children.begin(),children.end(),[](const State& a,const State& b){
            if(a.dead!=b.dead)return !a.dead;
            if(a.immediateDeath!=b.immediateDeath)return !a.immediateDeath;
            return a.value>b.value;
        });
        if(static_cast<int>(children.size())>width)children.erase(children.begin()+width,children.end());
        // Never replace a live root with a horizon where every continuation
        // is known dead. All alternatives remain in the candidate diagnostics.
        if(!children.front().dead || !haveBest) {best=children.front();haveBest=true;}
        beam=std::move(children);
        if(std::chrono::steady_clock::now()>=deadline)break;
    }
    if(haveBest) {
        answer.selected=best.root;answer.pose=best.rootPose;answer.rotation=best.rootRotation;answer.value=best.value;
        answer.attack=best.attack;answer.outgoing=best.outgoing;answer.cancelled=best.cancelled;
        answer.risen=best.risen;answer.b2b=best.status.b2b;answer.depth=best.depth;answer.dead=best.dead;
    }
    answer.searchMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    return answer;
}
} // namespace

bool enabled(const Environment& env) noexcept {return env.garbageModel==static_cast<int>(Runtime::GarbageMode::QuickPlay);}
bool plain(const Environment& env) noexcept {
    if(!enabled(env) || env.quickPlayExpert || env.quickPlayNoHold || env.quickPlayMessy ||
        env.quickPlayAllSpin || env.quickPlayFool || env.quickPlayGravity || env.quickPlayVolatile ||
        env.quickPlayDoubleHole || env.quickPlayBackfire || env.quickPlaySnakeEyes || env.quickPlaySnowball)return false;
    if(env.quickPlayRules && (Runtime::enabled(env.quickPlayRules->quickPlay.invisible) ||
        env.quickPlayRules->quickPlay.continuousCancellation))return false;
    return env.fieldWidth==10 && env.fieldHeight==20 && env.rotationSystem==0;
}
Environment native_environment(const Environment& env) {
    auto result=env;
    // Preserve the existing attack policy, including its conservative single
    // valuation. Actual QP's extra attack/XP is settled by the host, not used
    // to replace a bot's proven firepower policy with scattered singles.
    result.garbageModel=static_cast<int>(Runtime::GarbageMode::None);
    result.garbageBufferEnabled=false;
    result.openerDoubleCancelEnabled=false;
    return result;
}
std::optional<NativeBaseline> native_baseline(Profile profile,const Environment& env,
    const Runtime::PlayerSessionState& player,const Runtime::Playfield& field) {
    if(!plain(env) || field.width()!=10 || field.height()!=20)return {};
    const int active=player.gameplay.playfield.activeHeight;
    if(active<=0 || active>20)return {};
    NativeBaseline result;
    result.environment=native_environment(env);result.rowOffset=20-active;
    result.field=std::make_shared<Runtime::Playfield>();result.field->initialize(10,20);
    const int first=profile==Profile::While1?-5:-20;
    for(int y=-hidden;y<active;++y)for(int x=0;x<10;++x) {
        const auto cell=field.cell(y,x);
        if(cell.empty_cell())continue;
        if(y+result.rowOffset<first || cell.kind()==Runtime::BoardCellKind::TimedSolid ||
            cell.kind()==Runtime::BoardCellKind::TimedRelease || cell.kind()==Runtime::BoardCellKind::SolidGarbage)return {};
        result.field->set_cell(y+result.rowOffset,x,cell);
    }
    // Permanent fatigue rows become the native search floor rather than
    // ordinary full rows that its line clearer could incorrectly remove.
    result.player=std::make_shared<Runtime::PlayerSessionState>(player);
    auto& board=result.player->gameplay.playfield;
    board.width=board.visibleHeight=board.activeHeight=20;board.solidGarbageHeight=0;
    board.field=*result.field;
    for(const auto& windup:env.quickPlayProgress.windups) {
        const auto arrival=TimePoint::from_seconds(env.nowSeconds+std::max(0.0,windup.due-env.elapsedGameSeconds));
        result.player->garbage.buffer.push_back(Rules::make_garbage_packet(windup.attack,1,arrival,false,
            Duration::from_seconds(env.incomingGarbageDelaySeconds)));
    }
    return result;
}
bool native_answer_valid(const Environment& env,const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,const std::vector<int>& next,Piece::Pose start,int hold,
    int selected,Piece::Pose answer,Runtime::RotationRecord rotation) {
    if(next.empty() || selected<0 || (selected!=next.front() && (hold==-2 ||
        selected!=(hold>=0?hold:next.size()>1?next[1]:-1))))return false;
    const auto piece=BotPiece::definition(selected,env);const auto config=rules(env);
    if(selected!=next.front()) {
        const auto origin=spawn(initial(env,player,field),piece.type(),config);
        if(!origin)return false;
        start=*origin;
    }
    Engine::MovementState movement;movement.pose=start;
    return bool(Engine::search_autoplay_path(field,piece,movement,answer,rotation,
        config.handling,config.movement,config.handling.gravityInterval,false,
        [&]{return env.searchCancelled && env.searchCancelled->load();},Engine::autoplay_spin_rules(config)));
}
Autoplay::Availability availability(const Environment& env) {
    if(env.fieldWidth<4 || env.fieldWidth>32 || env.fieldHeight<1 || env.fieldHeight>40)
        return {false,"Quick Play bots require a width of 4-32 and a height of 1-40."};
    if(env.antiGravity)return {false,"Anti-gravity is unsupported."};
    if(env.holdEnabled && (env.infiniteHold || env.holdCount>1))return {false,"Bots support at most one hold per piece."};
    if(env.movement.disableHardDrop && !env.naturalGravityEnabled)return {false,"A bot needs hard drop or natural gravity to lock."};
    return {};
}
Forecast forecast_placement(const Environment& env,const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,Piece::Type type,Piece::Pose pose,Runtime::RotationRecord rotation,Duration afterLock) {
    auto state=initial(env,player,field);const auto config=rules(env);
    auto result=place(state,config,type,pose,rotation,env.pieceTime?afterLock.seconds():0);
    if(!env.pieceTime)advance_time(state,config,afterLock.seconds());
    result.application.state=state.status;
    return {state.board.materialize(),result.placement,result.application,state.garbage,
        state.activeHeight,state.board.height,state.solidHeight,state.garbageHeight,
        result.cancelled,state.risen,state.dead};
}
Decision search(Profile profile,const Environment& env,const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,const std::vector<int>& next,const std::vector<int>& unseen,
    Piece::Pose start,int hold,const ShapeEvaluator& shape) {
    return choose(profile,env,player,field,next,unseen,start,hold,shape);
}
} // namespace Bot::QuickPlaySearch
