#include "tetr/bot/quick_play_search.h"
#include "bot_piece_access.h"
#include "chatgpt/policy.h"
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
#include <string>
#include <tuple>
#include <unordered_set>

namespace Bot::QuickPlaySearch {
namespace {
constexpr int hidden=Runtime::kHiddenRowCapacity;
using Cell=Runtime::BoardCell;
using Duration=Runtime::Duration;
using TimePoint=Runtime::TimePoint;

const Piece::Definition& cached_piece(Piece::Type type) {
    static const auto definitions=[] {
        std::vector<Piece::Definition> result;result.reserve(Piece::catalog().size());
        for(const auto& entry:Piece::catalog())result.push_back(Piece::get_definition(entry.type));
        return result;
    }();
    return definitions.at(type.index());
}
const Piece::Definition& cached_piece(int logical,const Environment& env) {
    return cached_piece(logical>=0 && logical<7?env.pieceTypes[logical]:Piece::Type::from_index(logical));
}

// Search copies only the actual columns, while retaining the host's complete
// hidden area and cell kinds (piece, normal garbage, timed and permanent solid).
struct Board {
    int width=10,height=20;
    bool fast=false;
    // Conservative upper bound on occupied rows. Empty hidden rows need no
    // repeated rule scans; copies can only move this bound upward.
    int firstPossible=20;
    std::vector<Cell> cells;
    Board(int w,int h,bool accelerated=false):width(w),height(h),fast(accelerated),firstPossible(h),cells((hidden+h)*w) {}
    explicit Board(const Runtime::Playfield& field,bool accelerated=false):Board(field.width(),field.height(),accelerated) {
        for(int y=-hidden;y<height;++y) for(int x=0;x<width;++x) at(y,x)=field.cell(y,x);
        if(!fast)return;
        firstPossible=height;
        for(int y=-hidden;y<height;++y)for(int x=0;x<width;++x)if(get(y,x).occupied()) {
            firstPossible=y;return;
        }
    }
    Cell get(int y,int x) const {
        return y>=-hidden && y<height && x>=0 && x<width
            ? cells[(y+hidden)*width+x] : Cell::solid_garbage();
    }
    Cell& at(int y,int x) { if(fast)firstPossible=std::min(firstPossible,y);return cells[(y+hidden)*width+x]; }
    void copy_row(int destination,int source) {
        if(!fast) {for(int x=0;x<width;++x)at(destination,x)=get(source,x);return;}
        if(destination==source)return;
        if(source>=firstPossible)firstPossible=std::min(firstPossible,destination);
        std::copy_n(cells.data()+(source+hidden)*width,width,cells.data()+(destination+hidden)*width);
    }
    std::shared_ptr<Runtime::Playfield> materialize() const {
        auto field=std::make_shared<Runtime::Playfield>(); field->initialize(width,height);
        for(int y=fast?firstPossible:-hidden;y<height;++y) for(int x=0;x<width;++x) field->set_cell(y,x,get(y,x));
        return field;
    }
    std::vector<std::uint32_t> bits(int bottom) const {
        std::vector<std::uint32_t> result(bottom+hidden);
        for(int y=fast?firstPossible:-hidden;y<bottom;++y) for(int x=0;x<width;++x)
            if(get(y,x).occupied()) result[bottom-1-y]|=std::uint32_t{1}<<x;
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
    int policyAttack=0,maxCombo=0;
    double like=0;
    explicit State(const Runtime::Playfield& field,bool accelerated=false):board(field,accelerated) {}
};

State initial(const Environment& env,const Runtime::PlayerSessionState& player,const Runtime::Playfield& field,bool accelerated=false) {
    State s(field,accelerated);
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
    g.legacyQuickPlayRules=c.quickPlay.legacyRules;
    g.concentrate=Rules::qp_garbage_concentrates(c.quickPlay,s.floor,s.status.qpProgress,s.elapsed);
    g.digScores=Rules::qp_garbage_dig_scores(s.board.width,-hidden,s.activeHeight,
        [&](int y,int x){return s.board.get(y,x);},c.quickPlay.legacyRules);
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
int receive(State& s,const Runtime::RuntimeConfig& c,int attack,bool resolved=false) {
    int lines=attack;
    if(!resolved) {
        const double adjusted=Rules::qp_receive_attack(attack,s.status.qpAltitude,s.status.qpAltitude,
            s.floor,c.quickPlay,s.status.qpProgress,s.elapsed);
        const double exact=std::floor(adjusted)*std::clamp(c.garbage.hitMultiplier,0.0,1000.0)+
            (c.garbage.legacyIncomingRounding?0:s.garbage.incomingFraction);
        lines=static_cast<int>(std::min(std::floor(exact+(c.garbage.legacyIncomingRounding?0:1e-9)),double(std::numeric_limits<int>::max())));
        s.garbage.incomingFraction=c.garbage.legacyIncomingRounding?0:std::max(0.0,exact-lines);
    }
    if(lines<=0)return 0;
    auto& p=s.status.qpProgress;
    if(!c.quickPlay.legacyRules)p.piecesSinceRelief=0;
    p.receiveProtection=std::min(18,p.receiveProtection+(Runtime::reverse(c.quickPlay.volatileMode)?lines/3:lines));p.sinceReceived=0;
    s.garbage.buffer.push_back(Rules::make_garbage_packet(lines,1,TimePoint::from_seconds(s.clock),false,delay(s,c)));
    return lines;
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
        if(Rules::qp_gravity_increases(c.quickPlay) && s.clock+1e-9>=s.gravityUpdate+1) {s.gravity=Engine::advance_qp_gravity_speed(s.gravity,true);s.gravityUpdate+=1;}
    }
}

void grow(State& s,const Runtime::RuntimeConfig& c) {
    if(!Runtime::enabled(c.quickPlay.snowball))return;
    const int size=Rules::qp_snowball_size(s.status.totalLines);
    if(size<=s.board.width)return;
    const int oldWidth=s.board.width;
    const int offset=(size-4)/2-(oldWidth-4)/2,dh=size-s.board.height;
    Board bigger(size,size,s.board.fast);auto g=generation(s,c);g.width=size;
    for(int y=-hidden+dh;y<size;++y) {
        std::vector<Cell> old;
        for(int x=0;x<s.board.width;++x)old.push_back(s.board.get(y-dh,x));
        const auto row=Rules::expand_garbage_row(g,old,offset,s.garbage.random);
        for(int x=0;x<size;++x)if(!bigger.fast || row[x].occupied())bigger.at(y,x)=row[x];
    }
    s.board=std::move(bigger);s.activeHeight+=dh;
    for(int& x:s.garbage.holeOrder)x+=offset;
    for(int x=0;x<size;++x)if(x<offset || x>=offset+oldWidth) {
        if(std::find(s.garbage.holeOrder.begin(),s.garbage.holeOrder.end(),x)==s.garbage.holeOrder.end())s.garbage.holeOrder.push_back(x);
    }
    if(s.garbage.previousHoleColumn>=0)s.garbage.previousHoleColumn+=offset;
}

struct Projection {
    Rules::PlacementResult placement;
    Engine::PlacementApplicationResult application;
    int cancelled=0,policyAttack=0,family=-1;
    int preSelf=0,postSelf=0;
};

double feedback_cost(const State& previous,const Projection& p,const Bot_chatgpt::PolicyWeights& policy) {
    const int older=Rules::pending_garbage(previous.garbage.buffer);
    const int self=p.postSelf+std::max(0,p.preSelf-std::max(0,p.cancelled-older));
    return policy.attack*.65*self;
}

int policy_attack(const Rules::PlacementInput& in,const Rules::PlacementResult& p,int actualAttack) {
    if(!actualAttack)return 0;
    // Keep native firepower valuation (not QP's isolated-single/XP incentive),
    // while preserving genuine attack changes from the active modifiers.
    Rules::AttackInput attack;
    attack.linesCleared=p.clear.clearedLines;attack.spin=p.spin.spin;attack.mini=p.spin.mini;
    attack.perfectClear=p.clear.perfectClear;attack.halfClear=p.clear.halfClear;
    attack.clearedGarbageLines=p.clear.clearedGarbageLines;attack.piece=in.piece;
    attack.spinBonuses=in.spinBonuses;attack.attackRules=in.attackRules;
    attack.garbageMultiplier=in.garbageMultiplier;
    const auto score=[&](Runtime::GarbageMode mode) {
        const auto chain=Rules::update_chain({attack.linesCleared,attack.spin,attack.perfectClear,
            Runtime::AllSpinMode::Disabled,in.currentB2B,in.currentCombo,mode,in.attackRules,attack.halfClear});
        attack.garbageMode=mode;attack.b2b=chain.b2b;attack.combo=chain.combo;
        attack.previousB2B=chain.previousB2B;attack.b2bCharging=chain.b2bCharging;attack.b2bTransition=chain.b2bTransition;
        return Rules::calculate_attack(attack).attack;
    };
    const int nativeAttack=score(Runtime::GarbageMode::None);
    const int ordinaryQpAttack=score(Runtime::GarbageMode::QuickPlay);
    return static_cast<int>(std::clamp(static_cast<long long>(nativeAttack)+actualAttack-ordinaryQpAttack,
        0LL,static_cast<long long>(std::numeric_limits<int>::max())));
}
Projection place(State& s,const Runtime::RuntimeConfig& c,Piece::Type type,Piece::Pose pose,
    Runtime::RotationRecord rotation,double pieceTick=0) {
    const auto& piece=cached_piece(type);
    Rules::PlacementInput in;
    in.mapWidth=s.board.width;in.mapHeight=s.activeHeight;in.firstRow=-hidden;
    in.garbageStartRow=s.activeHeight-s.garbageHeight;in.layerStartRow=s.activeHeight-c.garbage.layerHeight;
    in.pose=pose;in.lastRotation=rotation;in.currentB2B=s.status.b2b;in.currentCombo=s.status.combo;
    in.garbageMode=Runtime::GarbageMode::QuickPlay;in.attackRoundingRoll=1;
    in.legacyQuickPlayRules=c.quickPlay.legacyRules;
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
    if(s.board.fast)in.firstRow=std::max(-hidden,s.board.firstPossible-1);
    Projection out;
    out.placement=Rules::resolve_placement(in,[&](int y,int x){return s.board.get(y,x);},
        [&](int y,int x){return s.board.get(y,x).occupied();});
    auto& p=out.placement;s.garbageHeight-=p.clear.clearedGarbageLines;
    Engine::PlacementApplicationConfig app;
    app.garbageMode=Runtime::GarbageMode::QuickPlay;app.qpMods=c.quickPlay;
    app.qpExpert=c.quickPlay.expert;app.qpVolatile=c.quickPlay.volatileMode;app.qpDoubleHole=c.quickPlay.doubleHole;
    app.qpAllSpin=c.quickPlay.allSpin;app.qpBackfire=c.quickPlay.backfire;app.qpSnakeEyes=c.quickPlay.snakeEyes;
    app.pieceFamily=metadata.familyIndex;app.pieceColumn=pose.position.col;
    if(metadata.familyIndex==0 && !c.quickPlay.legacyRules) {
        const auto& cells=piece.cells(pose.rotation);
        app.pieceColumn+=std::min_element(cells.begin(),cells.end(),
            [](const auto& a,const auto& b){return a[1]<b[1];})->at(1);
    }
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
    out.policyAttack=policy_attack(in,p,begin.effects.attack);out.family=metadata.familyIndex;
    out.preSelf=receive(s,c,begin.effects.preOffsetBufferLines);
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
        const auto decay=Engine::decay_timed_solid_garbage(s.board.width,s.board.fast?s.board.firstPossible:-hidden,s.board.height,
            [&](int y,int x){return s.board.get(y,x);},[&](int y,int x,Cell value){s.board.at(y,x)=value;},!s.board.fast);
        s.activeHeight+=decay.releasedRows;
    }
    out.postSelf=receive(s,c,out.application.effects.postOffsetBufferLines);
    // Keep two known-empty guard rows: the shared collapse plan clamps its
    // final source to firstRow+1, which must remain empty after a full clear.
    const auto moves=Rules::build_gravity_collapse_plan(s.board.fast?std::max(-hidden,s.board.firstPossible-2):-hidden,s.activeHeight,p.clear.clearedRows);
    Engine::apply_gravity_collapse(s.board.width,moves,[&](int y,int x){return s.board.get(y,x);},
        [&](int d,int from){s.board.copy_row(d,from);},!s.board.fast);
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

int roof(const Board& board,int bottom) {
    for(int y=board.fast?board.firstPossible:-hidden;y<bottom;++y)for(int x=0;x<board.width;++x)
        if(board.get(y,x).occupied())return bottom-y;
    return 0;
}

// ZZZ Botris v0.8's terrain terms, lifted out of its fixed tetromino graph.
// Removing the empty-board constant permits comparison across SB growth.
double terrain_impl(Profile profile,const std::vector<std::uint32_t>& rows,int w) {
    int top=static_cast<int>(rows.size());while(top && !rows[top-1])--top;
    const std::uint32_t full=w==32?~std::uint32_t{}:(std::uint32_t{1}<<w)-1;
    int columns=-2*top,transitions=top==0?0:-w,holes=0,holeCells=0,holeDepth=0,wellDepth=0;
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
            ++holes;holeCells+=Util::popcount32(missing);int cover=0;
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
    double rate=32,mul=.25;
    if(profile==Profile::Dig)value=-top*96-columns*160-transitions*128-holeCells*60-holes*380-wellDepth*100-holeDepth*40;
    if(profile==Profile::Qq) {value=-columns*32-transitions*32-holeCells*400-holes*38-wellDepth*16-holeDepth*4;rate=2;mul=0;}
    if(profile==Profile::C2) {
        value=-top*107.713-columns*109.116-transitions*116.58-holeCells*49.7899-holes*391.808-
            wellDepth*91.0623-holeDepth*67.7934;
        rate=34.2515;mul=.285739;
    }
    for(int cover:covers){value-=cover*rate;rate*=mul;}
    return value;
}

std::optional<Piece::Pose> spawn(const State& s,Piece::Type type,const Runtime::RuntimeConfig& c) {
    if(s.activeHeight<=0)return {};
    const auto& piece=cached_piece(type);
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
Rules::SpinResult spin_at(const State& s,const Piece::Definition& piece,Piece::Pose pose,
    Runtime::RotationRecord rotation,const Runtime::RuntimeConfig& c) {
    const auto metadata=Piece::metadata(piece.type());
    Rules::SpinInput spin;
    spin.spinChecker=metadata.spinCheck;spin.pose=pose;
    spin.lastRotation=Runtime::reverse(c.quickPlay.doubleHole) && !Piece::t_spin_check(metadata.spinCheck)
        ? Runtime::RotationRecord{}:rotation;
    spin.mapWidth=s.board.width;spin.mapHeight=s.activeHeight;
    spin.grounded=!can_place(s.board,piece,pose.translated(1,0));
    spin.immobile=spin.grounded && !can_place(s.board,piece,pose.translated(-1,0)) &&
        !can_place(s.board,piece,pose.translated(0,-1)) && !can_place(s.board,piece,pose.translated(0,1));
    spin.bonuses=c.spinBonuses;spin.allSpinEnabled=Runtime::enabled(c.quickPlay.allSpin);
    spin.noHoldReverse=Runtime::reverse(c.quickPlay.noHold);
    spin.rotationSystem=metadata.rotationSystem;spin.canonicalGeometry=metadata.tetrioRotation;
    return Rules::detect_spin(spin,[&](int y,int x) {
        if(s.board.get(y,x).occupied())return true;
        const auto& cells=piece.cells(pose.rotation);
        return std::find(cells.begin(),cells.end(),Piece::Offset{y-pose.position.row,x-pose.position.col})!=cells.end();
    });
}
std::uint64_t identity(const Engine::MovementState& movement) {
    return (std::uint64_t(movement.pose.position.row+256)<<24) |
        (std::uint64_t(movement.pose.position.col+128)<<12) |
        (std::uint64_t(movement.pose.rotation.index())<<8) |
        // Only rotation/no rotation and T's fifth kick change spin rewards.
        // Keep the original record in the node for execution and replay.
        (movement.lastRotation.rotated()?(movement.lastRotation.kick().index()==4?2:1):0);
}
std::vector<Landing> landings(const State& s,const Runtime::Playfield& field,Piece::Type type,
    Piece::Pose start,const Runtime::RuntimeConfig& c,const Environment& env,bool physicalDedup,
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::time_point::max()) {
    std::vector<Landing> result;
    const auto& piece=cached_piece(type);
    const auto metadata=Piece::metadata(type);
    std::array<Piece::Shape,4> normalized;
    std::array<Piece::Offset,4> minima{};
    std::array<int,4> shapeIds{};
    int padding=1;
    for(int r=0;r<4;++r) {
        normalized[r]=piece.cells(Runtime::Rotation::from_index(r));
        minima[r]=normalized[r].front();
        for(const auto& cell:normalized[r])for(int axis=0;axis<2;++axis) {
            minima[r][axis]=std::min(minima[r][axis],cell[axis]);
            padding=std::max(padding,std::abs(cell[axis])+1);
        }
        for(auto& cell:normalized[r])for(int axis=0;axis<2;++axis)cell[axis]-=minima[r][axis];
        std::sort(normalized[r].begin(),normalized[r].end());shapeIds[r]=r;
        for(int earlier=0;earlier<r;++earlier)if(normalized[r]==normalized[earlier]) {shapeIds[r]=shapeIds[earlier];break;}
    }
    const int columns=s.board.width+2*padding,rows=hidden+s.board.height+2*padding;
    const auto poseIndex=[&](Piece::Pose pose) {
        const int y=pose.position.row+hidden+padding,x=pose.position.col+padding;
        return y>=0 && y<rows && x>=0 && x<columns?(y*columns+x)*4+pose.rotation.index():-1;
    };
    std::vector<signed char> collisions(physicalDedup?rows*columns*4:0,-1);
    const auto canPlace=[&](Piece::Pose pose) {
        const int index=physicalDedup?poseIndex(pose):-1;
        if(index<0)return piece.can_place(pose,field);
        auto& value=collisions[index];
        if(value<0)value=piece.can_place(pose,field);
        return value!=0;
    };
    const auto tryRotate=[&](Piece::Pose& pose,Runtime::Rotation r,Piece::KickIndex& kick){return piece.try_rotate(pose,r,field,kick);};
    if(!canPlace(start))return result;
    Engine::MovementState root;root.pose=start;
    const bool instant=s.gravity.is_instant(),soft=Engine::soft_drop_interval(s.gravity,c.handling.softDropFactor,c.handling.softDropFixed)==Duration{};
    if(instant)Engine::apply_presentation_instant_gravity(root,canPlace);
    if(physicalDedup && !instant && env.partial_descent_available()) {
        // All orientations and their kicks fit in this empty band, across the
        // entire board. Descending only to its upper boundary preserves routes
        // onto side towers as well as the paths below the surface.
        int lowest=0,kickReach=0;
        for(int r=0;r<4;++r)for(const auto& cell:piece.cells(Runtime::Rotation::from_index(r)))lowest=std::max(lowest,cell[0]);
        for(const auto& tests:piece.kick_tests())for(const auto& kick:tests)kickReach=std::max(kickReach,std::abs(kick[1]));
        const int boundary=s.activeHeight-roof(s.board,s.activeHeight)-lowest-kickReach-1;
        if(root.pose.position.row<boundary)root.pose.position.row=boundary;
    }
    std::vector<Engine::MovementState> queue;queue.reserve(1024);queue.push_back(root);
    // Arrival rotation affects the landing classification, but never the
    // available outgoing moves. Emit every arrival, expand each pose once.
    std::unordered_set<std::uint64_t> visited;
    std::vector<bool> seen(physicalDedup?rows*columns*4:0);
    const auto visit=[&](const auto& movement) {
        const int index=physicalDedup?poseIndex(movement.pose):-1;
        if(index<0)return visited.insert(identity(movement)).second;
        if(seen[index])return false;
        seen[index]=true;return true;
    };
    visit(root);
    std::unordered_set<std::uint64_t> emitted;
    const auto emit=[&](Engine::MovementState movement) {
        if(physicalDedup && (!c.movement.disableSoftDrop || s.gravity.enabled()) &&
            canPlace(movement.pose.translated(1,0)))return;
        if(!c.movement.disableHardDrop)
            Engine::apply_movement_script_command(movement,Engine::MovementScriptCommand::HardDrop,canPlace,tryRotate);
        if(canPlace(movement.pose.translated(1,0)))return;
        if(c.movement.disableHardDrop && s.gravity.is_disabled())return;
        const auto classification=spin_at(s,piece,movement.pose,movement.lastRotation,c);
        // Symmetric rotations can occupy exactly the same cells. Keep one
        // executable representative per geometry and settled spin category;
        // otherwise equivalent O/I branches consume the narrow beam.
        std::uint64_t key;
        if(physicalDedup) {
            const int r=movement.pose.rotation.index();
            key=(std::uint64_t(movement.pose.position.row+minima[r][0]+256)<<24) |
                (std::uint64_t(movement.pose.position.col+minima[r][1]+128)<<12) |
                (std::uint64_t(shapeIds[r])<<8) | (classification.spin?(classification.mini?1:2):0);
        } else {
            const auto category=classification.spin?(classification.mini?1:2):0;
            key=(identity(movement)&~std::uint64_t{255})|category;
        }
        if(emitted.insert(key).second)result.push_back({movement.pose,movement.lastRotation});
    };
    std::vector<Engine::MovementScriptCommand> commands;
    for(char code:Engine::kMovementSearchCommands) {
        const auto command=Engine::decode_movement_script_command(code);
        if(Engine::movement_script_command_allowed(command,c.movement,env.wall_only_horizontal(),s.gravity.enabled(),instant,soft))
            commands.push_back(command);
    }
    if(physicalDedup)emit(root);
    for(std::size_t expanded=0;expanded<queue.size() && queue.size()<50000;++expanded) {
        if((expanded%128)==0 && std::chrono::steady_clock::now()>=deadline)break;
        if(env.searchCancelled && env.searchCancelled->load())break;
        const auto current=queue[expanded];
        if(!physicalDedup)emit(current);
        for(const auto command:commands) {
            auto next=current;
            const auto effect=Engine::apply_movement_script_command(next,command,canPlace,tryRotate,
                env.wall_only_horizontal(),env.arrSeconds==0,c.movement,instant,soft);
            if(effect.changed) {
                if(physicalDedup)emit(next);
                if(visit(next))queue.push_back(next);
            }
        }
    }
    return result;
}

double danger(const State& s,const Runtime::RuntimeConfig& c,double duration) {
    int announced=0;for(const auto& windup:s.status.qpProgress.windups)
        if(windup.due<=s.elapsed+duration)announced+=windup.attack;
    const double normal=Rules::qp_receive_attack(1,s.status.qpAltitude,s.status.qpAltitude,
        s.floor,Runtime::QuickPlayConfig{},Runtime::QuickPlayProgress{},s.elapsed);
    const double modified=Rules::qp_receive_attack(1,s.status.qpAltitude,s.status.qpAltitude,
        s.floor,c.quickPlay,s.status.qpProgress,s.elapsed);
    const double ownApm=s.status.totalAttack*60./std::max(30.,s.elapsed*c.timeMultiplier);
    const double fatigue=1+(s.elapsed>=540?.25:0)+(s.elapsed>=660?.25:0);
    const double uncertainty=Rules::qp_pressure(s.status.qpAltitude,ownApm).mean*fatigue*duration/60*
        (normal>0?modified/normal:0)*c.garbage.hitMultiplier;
    return std::max(0.0,roof(s.board,s.activeHeight)+std::min(c.garbage.cap,
        Rules::pending_garbage(s.garbage.buffer)+announced)+uncertainty-s.activeHeight*.6);
}

// A bounded terminal estimate for visible pieces: a straight descent followed
// by a real rotation into a grounded Spin. No guessed slots, hidden queue or
// invented garbage are used. Restricted movement uses the full search instead.
double spin_opportunity(const State& s,const Runtime::RuntimeConfig& c,const Environment& env,
    const std::vector<int>& next,const Bot_chatgpt::PolicyWeights& policy,const GeometryEnumerator& geometry) {
    if(s.dead || s.gravity.is_instant() || env.wall_only_horizontal() || !env.partial_descent_available() ||
        roof(s.board,s.activeHeight)>s.activeHeight-6)return 0;
    std::vector<int> visible;
    if(s.index<int(next.size()))visible.push_back(next[s.index]);
    if(env.holdEnabled && s.hold>=0 && (visible.empty() || s.hold!=visible.front()))visible.push_back(s.hold);
    if(visible.empty())return 0;
    const auto field=s.board.materialize();
    double best=0;
    for(int logical:visible) {
        const auto& piece=cached_piece(logical,env);
        const auto start=spawn(s,piece.type(),c);if(!start)continue;
        const auto consider=[&](Piece::Pose pose,Runtime::RotationRecord record) {
            if(!record.rotated() || !spin_at(s,piece,pose,record,c).spin)return;
            State projected=s;
            const auto p=place(projected,c,piece.type(),pose,record);
            if(projected.dead || !p.placement.spin.spin)return;
            const int lines=p.placement.clear.clearedLines;
            double gain=Bot_chatgpt::placement_reward(policy,p.policyAttack,p.placement.clear.clearedGarbageLines,
                lines,p.family,true,p.placement.clear.perfectClear,0)+policy.b2b*(projected.status.b2b-s.status.b2b);
            gain-=feedback_cost(s,p,policy);
            gain-=600*std::max(0,s.activeHeight-projected.activeHeight);
            best=std::max(best,gain);
        };
        if(geometry)if(auto targets=geometry(*field,s.activeHeight,piece.type(),*start,s.gravity)) {
            // The exact cached graph also finds slides and kicks below an
            // overhang. Do not limit setup scoring to straight-drop entries.
            for(const auto& target:*targets)consider(target.pose,target.rotation);
            continue;
        }
        const auto canPlace=[&](Piece::Pose pose){return can_place(s.board,piece,pose);};
        for(int r=0;r<4;++r) {
            const auto rotation=Runtime::Rotation::from_index(r);
            // An orientation is reachable in the empty spawn band only when
            // the configured rotation commands can actually produce it.
            if(r!=start->rotation.index() && !c.movement.allows_rotation(1) &&
                !c.movement.allows_rotation(-1) && !(c.movement.allows_rotation(2) && (r-start->rotation.index()+4)%4==2))continue;
            const auto& cells=piece.cells(rotation);int lo=cells.front()[1],hi=lo;
            for(const auto& cell:cells){lo=std::min(lo,cell[1]);hi=std::max(hi,cell[1]);}
            for(int x=-lo;x<s.board.width-hi;++x) {
                if(x<start->position.col && !c.movement.allows_horizontal(-1))continue;
                if(x>start->position.col && !c.movement.allows_horizontal(1))continue;
                Piece::Pose origin{{start->position.row,x},rotation};
                if(!canPlace(origin))continue;
                while(canPlace(origin.translated(1,0)))++origin.position.row;
                for(int delta:{1,-1,2}) {
                    if(!c.movement.allows_rotation(delta))continue;
                    auto pose=origin;Piece::KickIndex kick;
                    if(!piece.try_rotate(pose,rotation.rotated(delta),*field,kick) || canPlace(pose.translated(1,0)))continue;
                    const auto record=Runtime::RotationRecord::from_kick(kick);
                    consider(pose,record);
                }
            }
        }
    }
    return .55*best;
}

void evaluate(State& s,const State& previous,const Projection& p,Profile profile,
    const Runtime::RuntimeConfig& c,const ShapeEvaluator& custom,double pace,bool pieceTime,bool qpObjective,
    const Bot_chatgpt::PolicyWeights& policy) {
    const double discount=std::pow(.96,s.depth-1);
    const int lines=p.placement.clear.clearedLines,dug=p.placement.clear.clearedGarbageLines;
    const double delays=pieceTime?0:c.field.entryDelay.seconds()+(lines>0?c.field.lineClearDelay.seconds():0);
    if(profile==Profile::ChatGPT)
        s.reward+=discount*Bot_chatgpt::placement_reward(policy,p.policyAttack,dug,lines,
            p.family,p.placement.spin.spin,p.placement.clear.perfectClear,delays);
    else if(profile==Profile::While1 || profile==Profile::Toj || profile==Profile::Qq || profile==Profile::C2)
        s.reward+=discount*(512*p.policyAttack+512*dug-40*lines);
    else if(profile==Profile::Dig)
        s.reward+=discount*(900*dug+100*lines+128*p.policyAttack);
    else if(profile==Profile::Candy || profile==Profile::Mech)
        s.reward+=discount*(256*p.policyAttack+256*dug-40*lines+
            (p.placement.clear.perfectClear?(profile==Profile::Mech?12000:8000):0));
    else if(profile==Profile::ColdClear) {
        constexpr int clearReward[]={0,-143,-100,-58,390};
        constexpr int spinReward[]={0,121,410,602,800};
        const int n=std::clamp(lines,0,4);
        s.reward+=discount*(150*p.policyAttack+180*dug+
            (p.placement.clear.perfectClear?999:p.placement.spin.spin&&!p.placement.attack.effectiveMini?spinReward[n]:clearReward[n])-
            (p.family==5&&!p.placement.spin.spin?152:0));
    }
    if(profile==Profile::ChatGPT && qpObjective) {
        // Reward actual climb/booster work from the shared rules, including
        // cancellation, conversion, history discounts and upgrade fatigue.
        // Keep firepower dominant so compensated singles do not replace it.
        const auto energy=[](const State& state) {
            const double level=state.status.qpSpeed;
            return 2*level*(level-1)+state.status.qpProgress.experience;
        };
        const double climb=s.status.qpAltitude+s.status.qpProgress.pendingAltitude-
            previous.status.qpAltitude-previous.status.qpProgress.pendingAltitude;
        // Measure converted work rather than multiplying firepower again by
        // an already earned booster level. Otherwise late-game altitude can
        // overwhelm Spin construction and every physical terrain term.
        s.reward+=discount*(100*climb/std::max(1.,previous.status.qpSpeed)+40*(energy(s)-energy(previous)));
        // Generated self-garbage is an expense even while still buffered.
        // Credit only the part of pre-offset garbage actually cancelled after
        // the older FIFO packets; post-offset garbage cannot cancel itself.
        s.reward-=discount*feedback_cost(previous,p,policy);
        // Timed/permanent floor growth reduces the playable area even if its
        // shifted terrain happens to look flat. This includes repeat penalties.
        s.reward-=discount*600*std::max(0,previous.activeHeight-s.activeHeight);
    }
    // A row consumes a larger fraction of the reserve on a short playfield.
    // Preserve the twenty-row scale and the other BOTs' established policies.
    const double pressure=danger(s,c,pace)*(profile==Profile::ChatGPT?
        std::sqrt(20./std::max(4,s.activeHeight)):1.);
    s.pathRisk+=discount*35*pressure*pressure;
    const auto rows=s.board.bits(s.activeHeight);
    double shape=terrain_impl(profile,rows,s.board.width);
    const bool legacyShape=custom && (profile!=Profile::While1 || (s.board.width==10 && s.activeHeight<=24));
    if(legacyShape)
        shape=custom(rows,s.board.width,s.activeHeight,s.garbageHeight,s.status.b2b);
    s.value=shape+s.reward+(profile==Profile::ChatGPT?policy.b2b*s.status.b2b:0)+
        (s.status.b2b?300:0)-95*pressure*pressure-s.pathRisk;
    if(profile==Profile::ChatGPT && s.status.qpBlighted)s.value+=policy.attack*.5;
    if(profile==Profile::ChatGPT && (s.hold==0 || s.hold==5))s.value+=policy.holdReserve;
    if(profile==Profile::Botris) {
        // Botris's native attack, combo and preference terms. Use the shared
        // simulator's settlement, without adding a second burst/XP/well policy.
        s.policyAttack=previous.policyAttack+p.policyAttack;
        s.maxCombo=std::max(previous.maxCombo,std::max(0,s.status.combo));
        if(!lines && previous.status.combo>0 && previous.status.combo<3)s.like-=2;
        if(lines==1 && p.placement.spin.spin && !p.placement.attack.effectiveMini)s.like+=8;
        if(s.status.combo<6)s.like-=3*std::max(0,s.status.combo);
        if(p.placement.clear.perfectClear)s.like-=8;
        if(s.hold==0 && !lines)s.like+=2;
        int count=0;for(int y=-hidden;y<s.activeHeight;++y)for(int x=0;x<s.board.width;++x)
            count+=s.board.get(y,x).occupied();
        const double capacity=s.board.width*(s.board.height+4.0);
        const double scale=std::max(.05,(capacity-count)/capacity);
        s.value=shape+(s.policyAttack*(64+128*(3+1.0/s.depth))+
            (s.status.b2b?514:0)+64*s.like)*scale+
            s.maxCombo*(s.maxCombo-1)*40-95*pressure*pressure-s.pathRisk;
    }
    if(s.dead)s.value=(s.immediateDeath?-1e15:-1e12)+std::clamp(s.reward,-1e6,1e6);
}

Decision choose(Profile profile,const Environment& env,const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,const std::vector<int>& supplied,const std::vector<int>& unseen,
    Piece::Pose start,int hold,const ShapeEvaluator& shape,const GeometryEnumerator& geometry) {
    Decision answer;
    if(supplied.empty() || !availability(env))return answer;
    const auto started=std::chrono::steady_clock::now();
    // At the default quality, leave time for root validation/execution inside
    // the requested 100 ms cycle. Other native BOTs retain their own budget.
    const int budget=profile==Profile::ChatGPT?std::clamp(env.autoplaySearchWidth*3/10,5,90):
        std::clamp((env.autoplaySearchWidth+5)/6,5,250);
    const auto deadline=started+std::chrono::milliseconds(budget);
    const int width=profile==Profile::ChatGPT?std::max(1,env.autoplaySearchWidth):
        std::clamp((env.autoplaySearchWidth+11)/12,2,80);
    const double pace=env.secondsPerPiece>0?env.secondsPerPiece:env.autoplayPps>0?1/env.autoplayPps:.1;
    auto config=rules(env);
    auto next=supplied;
    // A singleton of the visible bag complement is inferable, unlike the
    // actual hidden queue. rNH and other randomizers cannot use this inference.
    if(!env.randomPieces && !config.quickPlay.continuousCancellation &&
        config.quickPlay.fool==Runtime::FoolMode::Disabled && unseen.size()==1 && unseen.front()>=0 && unseen.front()<7)
        next.push_back(unseen.front());
    const int depthLimit=profile==Profile::ChatGPT?int(next.size())+int(env.holdEnabled):profile==Profile::While1?5:4;
    answer.searchWidth=width;
    std::vector<State> beam;
    auto root=initial(env,player,field,profile==Profile::ChatGPT);root.hold=hold>=0?hold:-1;beam.push_back(root);
    const auto spinRules=Engine::autoplay_spin_rules(config);
    const bool fullSpins=!spinRules.suppressNonTSpins && Bot_chatgpt::full_non_t_spins(
        spinRules.allSpinEnabled?Runtime::SpinBonuses::AllSpinPlus:spinRules.bonuses);
    const auto policy=profile==Profile::ChatGPT?
        Bot_chatgpt::capability_policy({roof(root.board,root.activeHeight),root.garbageHeight,
            Rules::pending_garbage(root.garbage.buffer),0},root.activeHeight,env.holdEnabled,fullSpins):
        Bot_chatgpt::PolicyWeights{};
    Bot_chatgpt::TerrainCapabilities capabilities;
    capabilities.canHold=env.holdEnabled;
    capabilities.constrainedMovement=env.wall_only_horizontal() || !env.partial_descent_available() || root.gravity.is_instant();
    if(hold==-2 && env.holdEnabled && player.gameplay.hold.piece.valid()) {
        root.hold=player.gameplay.hold.piece.index();
        for(int family=0;family<7;++family)if(env.pieceTypes[family]==player.gameplay.hold.piece)root.hold=family;
        beam.front().hold=root.hold;
    }
    State best=root;bool haveBest=false;
    std::vector<std::vector<State>> frontier(profile==Profile::ChatGPT?depthLimit:0);
    std::vector<State> deepest;
    if(profile==Profile::ChatGPT)frontier[0]=beam;
    int cursor=0;
    for(int iteration=0;;++iteration) {
        int depth=iteration;
        if(profile==Profile::ChatGPT) {
            if(iteration && std::chrono::steady_clock::now()>=deadline)break;
            if(env.searchCancelled && env.searchCancelled->load())break;
            int checked=0;
            while(frontier[cursor].empty() && checked<depthLimit) {cursor=(cursor+1)%depthLimit;++checked;}
            if(checked==depthLimit)break;
            depth=cursor;cursor=(cursor+1)%depthLimit;
            // Progress through every visible depth, then return for the next
            // alternative. A full layer is not required before deepening, and
            // the configured width remains available at every depth.
            beam.clear();beam.push_back(std::move(frontier[depth].front()));
            frontier[depth].erase(frontier[depth].begin());
        } else if(depth>=depthLimit || beam.empty())break;
        std::vector<State> children;
        for(const auto& parent:beam) {
            const bool heldTail=profile==Profile::ChatGPT && env.holdEnabled && parent.hold>=0 &&
                parent.index==int(next.size()) && !player.pieces.allowedPieces.empty();
            if(parent.dead || (parent.index>=int(next.size()) && !heldTail))continue;
            // A known held piece remains playable beyond NEXT only if every
            // publicly allowed unknown current piece can spawn before HOLD.
            // Its identity and the new held piece are never guessed.
            if(heldTail && std::any_of(player.pieces.allowedPieces.begin(),player.pieces.allowedPieces.end(),
                [&](auto type){return !spawn(parent,type,config);}))continue;
            if(depth && std::chrono::steady_clock::now()>=deadline)break;
            if(env.searchCancelled && env.searchCancelled->load())break;
            const auto image=parent.board.materialize();
            const int current=heldTail?-1:next[parent.index];
            for(bool useHold:{false,true}) {
                if(heldTail && !useHold)continue;
                const bool canHold=env.holdEnabled && profile!=Profile::Dig && profile!=Profile::Qq && profile!=Profile::C2 &&
                    !(depth==0 && hold==-2);
                if(useHold && (!canHold || (parent.hold<0 && parent.index+1>=static_cast<int>(next.size()))))continue;
                const int selected=useHold?(parent.hold>=0?parent.hold:next[parent.index+1]):current;
                if(useHold && selected==current)continue;
                if(selected<0)continue;
                const auto& piece=cached_piece(selected,env);
                const auto origin=depth==0 && !useHold ? std::optional<Piece::Pose>{start} : spawn(parent,piece.type(),config);
                if(!origin)continue;
                std::vector<Landing> targets;
                auto accelerated=geometry?geometry(*image,parent.activeHeight,piece.type(),*origin,parent.gravity):
                    std::optional<std::vector<Autoplay::PlacementCandidate>>{};
                if(accelerated)for(const auto& landing:*accelerated)targets.push_back({landing.pose,landing.rotation});
                else targets=landings(parent,*image,piece.type(),*origin,config,env,profile==Profile::ChatGPT,
                    profile==Profile::ChatGPT && env.autoplaySearchWidth>=150 && next.size()>1?deadline:
                    std::chrono::steady_clock::time_point::max());
                for(const auto& landing:targets) {
                    if(!children.empty() && profile==Profile::ChatGPT && env.autoplaySearchWidth>=150 &&
                        std::chrono::steady_clock::now()>=deadline)break;
                    State candidate=parent;
                    candidate.index+=1+(useHold && parent.hold<0);
                    if(useHold)candidate.hold=current;
                    if(depth==0) {candidate.root=selected;candidate.rootPose=landing.pose;candidate.rootRotation=landing.rotation;}
                    const auto projection=place(candidate,config,piece.type(),landing.pose,landing.rotation,env.pieceTime?pace:0);
                    ++answer.evaluated;
                    if(!env.pieceTime)advance_time(candidate,config,std::max(pace,
                        env.entryDelaySeconds+(projection.placement.clear.clearedLines>0?env.lineClearDelaySeconds:0)));
                    if(candidate.index<static_cast<int>(next.size()) && !spawn(candidate,BotPiece::definition(next[candidate.index],env).type(),config))
                        candidate.dead=true;
                    evaluate(candidate,parent,projection,profile,config,shape,pace,env.pieceTime,!native_policy_compatible(env),policy);
                    if(profile==Profile::ChatGPT && !candidate.dead) {
                        auto physical=capabilities;physical.narrowPieceAvailable=false;
                        const auto available=[&](int logical) {
                            if(logical<0)return;
                            const auto& definition=cached_piece(logical,env);
                            const auto metadata=Piece::metadata(definition.type());
                            physical.largestPiece=std::max(physical.largestPiece,int(definition.cells({}).size()));
                            for(int r=0;r<4;++r) {
                                const int turns=(r-metadata.spawnRotation+4)%4;
                                if(turns && !config.movement.allows_rotation(1) && !config.movement.allows_rotation(-1) &&
                                    !(turns==2 && config.movement.allows_rotation(2)))continue;
                                const auto& cells=definition.cells(Runtime::Rotation::from_index(r));
                                int lo=cells.front()[1],hi=lo;
                                for(const auto& cell:cells){lo=std::min(lo,cell[1]);hi=std::max(hi,cell[1]);}
                                if(hi==lo)physical.narrowPieceAvailable=true;
                            }
                        };
                        if(env.holdEnabled)available(candidate.hold);
                        for(int i=candidate.index;i<std::min(int(next.size()),candidate.index+3);++i)available(next[i]);
                        const auto rows=candidate.board.bits(candidate.activeHeight);
                        candidate.value+=Bot_chatgpt::capability_terrain(rows.data(),int(rows.size()),candidate.board.width,
                            candidate.garbageHeight,physical);
                    }
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
        if(profile==Profile::ChatGPT && depth==0 && config.spinBonuses!=Runtime::SpinBonuses::None) {
            // Only a shortlist needs the more expensive visible continuation
            // estimate. It is terminal value, never accumulated attack/reward.
            const int limit=std::min(int(children.size()),width*3);
            std::vector<double> original;original.reserve(limit);
            for(int i=0;i<limit;++i)original.push_back(children[i].value);
            bool estimated=true;
            for(int i=0;i<limit;++i) {
                if(std::chrono::steady_clock::now()>=deadline) {estimated=false;break;}
                children[i].value+=std::pow(.96,children[i].depth)*spin_opportunity(children[i],config,env,next,policy,geometry);
            }
            // Never reward only the prefix for which a terminal estimate fit
            // in the budget. The completed placement layer remains usable.
            if(!estimated)for(int i=0;i<limit;++i)children[i].value=original[i];
            std::stable_sort(children.begin(),children.end(),[](const State& a,const State& b){
                if(a.dead!=b.dead)return !a.dead;
                if(a.immediateDeath!=b.immediateDeath)return !a.immediateDeath;
                return a.value>b.value;
            });
        }
        if(profile==Profile::ChatGPT && depth+1<depthLimit) {
            auto& waiting=frontier[depth+1];
            for(auto& candidate:waiting)children.push_back(std::move(candidate));
            waiting.clear();
            std::stable_sort(children.begin(),children.end(),[](const State& a,const State& b){
                if(a.dead!=b.dead)return !a.dead;
                if(a.immediateDeath!=b.immediateDeath)return !a.immediateDeath;
                return a.value>b.value;
            });
        }
        if(profile==Profile::ChatGPT && int(children.size())>width) {
            // Retain distinct continuation permissions before filling the beam
            // by score. HOLD/queue consumption and repeat classes change which
            // follow-ups are legal; many copies of one class must not crowd
            // all the alternatives out of a finite beam.
            std::vector<std::size_t> retained;
            std::unordered_set<std::uint64_t> classes;
            for(std::size_t i=0;i<children.size() && int(retained.size())<std::max(1,width/3);++i) {
                const auto& child=children[i];std::uint64_t operation=0;
                if(child.status.qpLastOperation) {
                    const auto& op=*child.status.qpLastOperation;
                    if(config.quickPlay.allSpin==Runtime::AllSpinMode::Reverse)operation=op.spin?op.linesCleared+2:1;
                    else if(config.quickPlay.allSpin==Runtime::AllSpinMode::Enabled)
                        operation=1+op.linesCleared+8*op.spin+16*op.mini+(op.spin?32*op.pieceType.index():0);
                }
                const auto key=(std::uint64_t(child.status.qpBlighted)<<56)|(std::uint64_t(child.status.b2b>0)<<55)|
                    (operation<<24)|(std::uint64_t(child.index)<<16)|std::uint64_t(child.hold+1);
                if(classes.insert(key).second)retained.push_back(i);
            }
            for(std::size_t i=0;int(retained.size())<width;++i)
                if(std::find(retained.begin(),retained.end(),i)==retained.end())retained.push_back(i);
            std::sort(retained.begin(),retained.end());
            std::vector<State> diverse;diverse.reserve(width);
            for(auto i:retained)diverse.push_back(std::move(children[i]));
            children=std::move(diverse);
        } else if(static_cast<int>(children.size())>width)children.erase(children.begin()+width,children.end());
        // Never replace a live root with a horizon where every continuation
        // is known dead. All alternatives remain in the candidate diagnostics.
        if(profile==Profile::ChatGPT) {
            const auto& candidate=children.front();
            if(!haveBest || (!candidate.dead && (best.dead || candidate.depth>best.depth ||
                (candidate.depth==best.depth && candidate.value>best.value)))) {
                best=candidate;haveBest=true;deepest=children;
            }
            if(depth+1<depthLimit) {
                auto& waiting=frontier[depth+1];
                for(auto& candidate:children)if(!candidate.dead && (candidate.index<int(next.size()) ||
                    (candidate.index==int(next.size()) && env.holdEnabled && candidate.hold>=0)))waiting.push_back(std::move(candidate));
                answer.peakFrontier=std::max(answer.peakFrontier,int(waiting.size()));
            }
        } else {
            if(!children.front().dead || !haveBest) {best=children.front();haveBest=true;}
            beam=std::move(children);
        }
        if(std::chrono::steady_clock::now()>=deadline)break;
    }
    if(profile==Profile::ChatGPT)beam=std::move(deepest);
    if(profile==Profile::ChatGPT && !env.holdEnabled &&
        Runtime::effective_bag_randomizer(config)==Runtime::BagRandomizer::TotalMayhem &&
        !player.pieces.allowedPieces.empty() && player.pieces.allowedPieces.size()<=16) {
        // Unknown random pieces are chance outcomes, never choices. Complete
        // every allowed type before accepting an expectation; an interrupted
        // sample cannot selectively omit an inconvenient piece.
        double expectedBest=-1e100;int completed=0;
        for(const auto& parent:beam) {
            if(parent.dead || parent.index<int(next.size()) || completed>=4 ||
                std::chrono::steady_clock::now()>=deadline)continue;
            const auto image=parent.board.materialize();
            double total=0,worst=1e100;bool finished=true;
            for(const auto type:player.pieces.allowedPieces) {
                if(std::chrono::steady_clock::now()>=deadline || (env.searchCancelled && env.searchCancelled->load())) {finished=false;break;}
                double value=-1e12;
                if(const auto origin=spawn(parent,type,config)) {
                    std::vector<Landing> targets;
                    auto accelerated=geometry?geometry(*image,parent.activeHeight,type,*origin,parent.gravity):
                        std::optional<std::vector<Autoplay::PlacementCandidate>>{};
                    if(accelerated)for(const auto& landing:*accelerated)targets.push_back({landing.pose,landing.rotation});
                    else targets=landings(parent,*image,type,*origin,config,env,true,deadline);
                    for(const auto& landing:targets) {
                        if(std::chrono::steady_clock::now()>=deadline) {finished=false;break;}
                        State candidate=parent;
                        const auto projection=place(candidate,config,type,landing.pose,landing.rotation,env.pieceTime?pace:0);
                        if(!env.pieceTime)advance_time(candidate,config,std::max(pace,env.entryDelaySeconds+
                            (projection.placement.clear.clearedLines>0?env.lineClearDelaySeconds:0)));
                        evaluate(candidate,parent,projection,profile,config,shape,pace,env.pieceTime,true,policy);
                        const auto rows=candidate.board.bits(candidate.activeHeight);
                        auto physical=capabilities;physical.narrowPieceAvailable=false;
                        candidate.value+=Bot_chatgpt::capability_terrain(rows.data(),int(rows.size()),candidate.board.width,
                            candidate.garbageHeight,physical);
                        value=std::max(value,candidate.value);
                    }
                }
                if(!finished)break;
                total+=value;worst=std::min(worst,value);
            }
            if(!finished)break;
            const double expectation=.8*total/player.pieces.allowedPieces.size()+.2*worst;
            if(expectation>expectedBest) {best=parent;best.value=expectation;haveBest=true;expectedBest=expectation;}
            ++completed;
        }
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

double terrain(Profile profile,const std::vector<std::uint32_t>& rows,int width) {
    return terrain_impl(profile,rows,width);
}
bool enabled(const Environment& env) noexcept {return env.garbageModel==static_cast<int>(Runtime::GarbageMode::QuickPlay);}
bool native_policy_compatible(const Environment& env) noexcept {
    if(!enabled(env) || env.quickPlayNoHold==2 || (env.quickPlayAllSpin && env.quickPlayAllSpin!=3) || env.quickPlayFool ||
        env.quickPlayDoubleHole==2 || env.quickPlayBackfire || env.quickPlaySnakeEyes || env.quickPlaySnowball)return false;
    if(env.quickPlayRules && env.quickPlayRules->quickPlay.continuousCancellation)return false;
    return env.fieldWidth==10 && env.fieldHeight==20 && env.rotationSystem==0;
}
bool native_policy_compatible(Profile profile,const Environment& env) noexcept {
    if(profile!=Profile::ChatGPT || (env.quickPlayAllSpin!=1 && env.quickPlayAllSpin!=2))
        return native_policy_compatible(env);
    // ChatGPT's native evaluator carries the actual repeat class and rejects
    // fatal repeats. Board-changing combinations still use the rule search.
    auto placement=env;placement.quickPlayAllSpin=3;
    return native_policy_compatible(placement);
}
Environment native_environment(const Environment& env) {
    auto result=env;
    // Preserve the existing attack policy, including its conservative single
    // valuation. Actual QP's extra attack/XP is settled by the host, not used
    // to replace a bot's proven firepower policy with scattered singles.
    result.garbageModel=static_cast<int>(Runtime::GarbageMode::None);
    result.garbageBufferEnabled=false;
    result.openerDoubleCancelEnabled=false;
    // Native availability is checked against effective capabilities below.
    // The live host keeps the full QP config and settles all modifier effects.
    result.quickPlayExpert=result.quickPlayNoHold=result.quickPlayMessy=result.quickPlayGravity=0;
    result.quickPlayVolatile=result.quickPlayDoubleHole=0;
    result.quickPlayAllSpin=0; // QP-specific history remains in quickPlayRules.
    return result;
}
std::optional<NativeBaseline> native_baseline(Profile profile,const Environment& env,
    const Runtime::PlayerSessionState& player,const Runtime::Playfield& field) {
    if(!native_policy_compatible(profile,env) || field.width()!=10 || field.height()!=20)return {};
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
    board.width=10;board.visibleHeight=board.activeHeight=20;board.solidGarbageHeight=0;
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
    if(env.quickPlayAllSpin==1 || env.quickPlayAllSpin==2) {
        if(!piece.can_place(answer,field))return false;
        auto projected=initial(env,player,field);
        place(projected,config,piece.type(),answer,rotation);
        if(projected.dead)return false;
    }
    Engine::MovementState movement;movement.pose=start;
    if(!piece.can_place(movement.pose,field))return false;
    if(config.handling.gravityInterval.is_instant())Engine::apply_presentation_instant_gravity(movement,
        [&](Piece::Pose pose){return piece.can_place(pose,field);});
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
    auto state=initial(env,player,field,true);const auto config=rules(env);
    auto result=place(state,config,type,pose,rotation,env.pieceTime?afterLock.seconds():0);
    if(!env.pieceTime)advance_time(state,config,afterLock.seconds());
    result.application.state=state.status;
    return {state.board.materialize(),result.placement,result.application,state.garbage,
        state.activeHeight,state.board.height,state.solidHeight,state.garbageHeight,
        result.cancelled,state.risen,state.dead};
}
Decision search(Profile profile,const Environment& env,const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,const std::vector<int>& next,const std::vector<int>& unseen,
    Piece::Pose start,int hold,const ShapeEvaluator& shape,const GeometryEnumerator& geometry) {
    return choose(profile,env,player,field,next,unseen,start,hold,shape,geometry);
}
Decision adapt(Profile profile,const Environment& env,const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,const std::vector<int>& next,const std::vector<int>& unseen,
    Piece::Pose start,int hold,const NativePlanner& native,const ShapeEvaluator& shape) {
    if(native && native_policy_compatible(env))if(auto baseline=native_baseline(profile,env,player,field)) {
        auto result=native(*baseline);
        if(native_answer_valid(env,player,field,next,start,hold,result.selected,result.pose,result.rotation)) {
            result.usedNative=true;return result;
        }
    }
    return search(profile,env,player,field,next,unseen,start,hold,shape);
}
} // namespace Bot::QuickPlaySearch
