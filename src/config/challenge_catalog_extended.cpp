#include "tetr/challenges/challenges.h"
#include "tetr/config/presets.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/challenge_puzzles.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace Challenges {
namespace {
using Id=UserConfig::SettingId;
using V=UserConfig::PresetValue;
constexpr double inf=std::numeric_limits<double>::infinity();
void put(Definition& d,Constraint c) {
    auto it=std::find_if(d.constraints.begin(),d.constraints.end(),[&](const auto& old){return old.setting==c.setting;});
    if(it==d.constraints.end())d.constraints.push_back(std::move(c));else *it=std::move(c);
}
void fixed(Definition& d,Id id,V value){put(d,{id,Restriction::Fixed,{std::move(value)}});}
void range(Definition& d,Id id,double a,double b){
    Constraint c{id,Restriction::Interval,{},a,b,true,std::isfinite(b)};
    const double preferred=std::isfinite(b)?b:a;
    if(std::holds_alternative<int UserConfig::Settings::*>(UserConfig::setting_descriptor(id).member))c.preset=int(preferred);
    else c.preset=preferred;
    put(d,std::move(c));
}
void erase(Definition& d,Id id){d.constraints.erase(std::remove_if(d.constraints.begin(),d.constraints.end(),[&](const auto& c){return c.setting==id;}),d.constraints.end());}
void construction_hint(Definition& d,int rule) {
    Constraint c{Id::ChallengeRules,Restriction::Set,{0,rule}};c.preset=rule;put(d,std::move(c));
}
void goal(Definition& d,Progress progress,int target) {
    d.progress=progress;d.target=target;
    for(auto id:{Id::RacingDistance,Id::GarbageTarget,Id::AttackTarget,Id::BlitzTime,Id::AllClearTarget})erase(d,id);
    const int type=progress==Progress::Lines?2:progress==Progress::Garbage?3:progress==Progress::Attack?5:progress==Progress::AllClears?6:progress==Progress::Seconds?1:0;
    fixed(d,Id::Goal,type);
    if(type) {
        const auto id=type==2?Id::RacingDistance:type==3?Id::GarbageTarget:type==5?Id::AttackTarget:type==6?Id::AllClearTarget:Id::BlitzTime;
        fixed(d,id,type==1?V{double(target)}:V{target});
    }
}
Definition base(std::string id,std::string name,std::string category,Progress p=Progress::Lines,int target=40) {
    auto d=*find("sprint-40");d.id=std::move(id);d.name=name;d.chineseName=std::move(name);d.categories={std::move(category)};
    goal(d,p,target);d.endless=target>=1000000000;return d;
}
void attack_rules(Definition& d) {
    const UserConfig::Settings defaults;
    for(auto id:{Id::SpinBonuses,Id::B2BReward,Id::B2BRewardAmount,Id::B2BCharging,Id::B2BChargeThreshold,Id::B2BChargeReduction,
        Id::PerfectClearEnabled,Id::PerfectClearAttack,Id::PerfectClearCharge,Id::HalfClearEnabled,Id::HalfClearAttack,
        Id::HalfClearCharge,Id::GarbageSpecialAttack,Id::HitMultiplier,Id::CancellationMultiplier,Id::AttackMultiplier})
        fixed(d,id,UserConfig::Presets::read_value(defaults,id));
}
void normal_gravity(Definition& d,double interval=.6,double lock=.5) {
    range(d,Id::GravityInterval,0,interval);range(d,Id::LockDelay,0,lock);fixed(d,Id::InfiniteMovement,0);range(d,Id::LockResetLimit,0,15);
    fixed(d,Id::GravityProgression,0);
}
void twenty_g(Definition& d,double lock=.5) {normal_gravity(d,.6,lock);fixed(d,Id::InstantGravity,1);}
void thinking(Definition& d) {
    erase(d,Id::GravityInterval);fixed(d,Id::GravityProgression,0);range(d,Id::LockDelay,0,10.0);
}
void marathon(Definition& d) {normal_gravity(d);fixed(d,Id::GravityProgression,2);fixed(d,Id::GravityLevelLines,10);fixed(d,Id::GravityAcceleration,1.0);}
void classic_handling(Definition& d,double interval,double lock,int resets) {
    // Slow handling is part of this challenge. Share the project's Classic
    // preset values, then pin the distinct gravity/lock standard for each tier.
    static const auto reference=[] {
        UserConfig::Settings settings;
        for(const auto& [id,value]:UserConfig::Presets::definition(UserConfig::PresetKind::Classic).fields)
            UserConfig::Presets::write_value(settings,id,value);
        return UserConfig::effective_handling_settings(settings);
    }();
    for(const auto id:{Id::Das,Id::Arr,Id::Dcd,Id::SoftDropFactor,Id::SoftDropFixed,Id::PreMove,Id::DasPrecharge,
        Id::EntryDelay,Id::LineClearDelay,Id::DisableHardDrop,Id::Disable180})
        fixed(d,id,UserConfig::Presets::read_value(reference,id));
    fixed(d,Id::GravityInterval,interval);fixed(d,Id::InstantGravity,0);
    fixed(d,Id::LockDelay,lock);fixed(d,Id::LockResetLimit,resets);fixed(d,Id::InfiniteMovement,0);
    fixed(d,Id::GravityProgression,1);fixed(d,Id::GravityLevelLines,10);fixed(d,Id::GravityAcceleration,1.0);
}
void pure_quad(Definition& d) {d.stopRule=[] {return [](const Placement& p,const Result&,const Result&){return p.lines && p.lines!=4;};};}
void visibility(Definition& d,double delay) {
    fixed(d,Id::FieldInvisible,1);range(d,Id::InvisibilityDelay,0,delay);
}
}
std::vector<Definition> additional_definitions() {
    static const auto catalog=[] {
        std::vector<Definition> out;
        const auto add=[&](Definition d){
            const bool noNext=std::any_of(d.constraints.begin(),d.constraints.end(),[](const auto& c){return c.setting==Id::NextCount && c.accepts(0) && !c.accepts(1);});
            const auto variant=std::find_if(d.constraints.begin(),d.constraints.end(),[](const auto& c){return c.setting==Id::ChallengeRules;});
            const bool hiddenActive=variant!=d.constraints.end() && (variant->accepts(1) || variant->accepts(2) || variant->accepts(3));
            if(noNext || hiddenActive || d.id=="invisible-200-3")fixed(d,Id::ShowSpawnPosition,0);
            if(d.description.empty()) {
                d.description="手动操作，禁止暂停和撤销。";
                if(d.progress==Progress::Lines && !d.endless)d.description+="目标 "+std::to_string(int(d.target))+" 行。";
                if(d.progress==Progress::Garbage)d.description+="目标挖掘 "+std::to_string(int(d.target))+" 行。";
                if(d.progress==Progress::Attack)d.description+="目标产生 "+std::to_string(int(d.target))+" 攻击。";
                if(d.completedOnly)d.description+="仅记录完成目标的对局。";
                if(d.requiresB2b)d.description+="至少衔接一次 B2B，起链后禁止断链。";
            }
            out.push_back(std::move(d));
        };
        add(base("sprint-400","400 行","Sprint",Progress::Lines,400));
        auto d=base("sprint-m123-40","一至三连块 40 行","Sprint");fixed(d,Id::RotationSystem,4);add(d);
        d=base("sprint-no-move-40","无移动 40 行","Sprint");fixed(d,Id::DisableLeft,1);fixed(d,Id::DisableRight,1);add(d);
        d=base("sprint-frictionless-40","无摩擦 40 行","Sprint");twenty_g(d);fixed(d,Id::Das,0.0);fixed(d,Id::Arr,0.0);add(d);
        for(int tier=0;tier<3;++tier) {
            d=base(tier==0?"sprint-no-next-40":tier==1?"sprint-no-next-hold-40":"sprint-mph-40",
                   tier==0?"无预览 40 行":tier==1?"无预览无暂存 40 行":"MPH 40 行","Sprint");
            fixed(d,Id::NextCount,0);if(tier)fixed(d,Id::HoldEnabled,0);if(tier==2)fixed(d,Id::RandomPieces,1);add(d);
        }
        d=base("attack-100","100 攻击竞速","Attack and efficiency",Progress::Attack,100);attack_rules(d);add(d);
        d=base("efficiency-40","40 行攻击效率","Attack and efficiency");attack_rules(d);thinking(d);d.secondary=Secondary::Apl;d.completedOnly=true;add(d);
        for(bool plus:{false,true})for(int tier=0;tier<3;++tier) {
            d=base(std::string(plus?"research-plus-":"research-")+std::to_string(tier),std::string(plus?"科研+：":"科研：")+std::vector<std::string>{"普通","困难","疯狂"}[tier],"Attack and efficiency",Progress::Attack,100);
            attack_rules(d);if(tier==0)normal_gravity(d,1.0,5);else if(tier==1)normal_gravity(d);else twenty_g(d);
            d.requiresB2b=!plus;
            d.stopRule=[plus] {return [plus](const Placement& p,const Result&,const Result&){return plus?(p.lines && !p.spin && !p.allClear):p.b2bBroken;};};
            add(d);
        }
        for(int tier=0;tier<4;++tier) {
            d=base("backfire-100-"+std::to_string(tier),"自攻 100："+std::vector<std::string>{"普通","困难","疯狂","极限"}[tier],"Attack and efficiency",Progress::Attack,100);
            attack_rules(d);fixed(d,Id::GarbageMode,1);range(d,Id::GarbageInTime,0,std::vector<double>{2,1,.5,0}[tier]);
            fixed(d,Id::GarbageEntryMode,0);fixed(d,Id::GarbageRiseInterval,0.0);fixed(d,Id::GarbageCap,8);
            fixed(d,Id::MessinessWithinAttack,0.0);fixed(d,Id::AvoidSameColumn,0);fixed(d,Id::HoleCount,1);add(d);
        }
        for(bool blitz:{false,true}) {
            d=base(blitz?"score-blitz-120":"score-120",blitz?"120 秒加速打分":"120 秒限时打分","Score and endurance",Progress::Seconds,120);
            attack_rules(d);normal_gravity(d);fixed(d,Id::GravityInterval,.6);fixed(d,Id::GravityProgression,blitz?3:0);
            d.secondary=Secondary::Points;d.completedOnly=true;add(d);
        }
        d=base("zen-200","禅 200 行","Score and endurance",Progress::Lines,200);attack_rules(d);thinking(d);fixed(d,Id::GravityInterval,1e9+5.0);fixed(d,Id::InstantGravity,0);d.secondary=Secondary::Points;add(d);
        d=base("marathon-endless","无尽马拉松","Score and endurance",Progress::Lines,1000000000);marathon(d);attack_rules(d);fixed(d,Id::Goal,0);erase(d,Id::RacingDistance);d.secondary=Secondary::Points;add(d);
        d=*find("dig-100");d.id="dig-400";d.name=d.chineseName="挖掘 400 行";goal(d,Progress::Garbage,400);add(d);
        for(int lines:{10,20,40,100,400}) {
            d=*find("dig-40");d.id="dig-efficiency-"+std::to_string(lines);d.name=d.chineseName="挖掘效率 "+std::to_string(lines)+" 行";d.categories={"Dig","Efficiency"};goal(d,Progress::Garbage,lines);
            thinking(d);d.secondary=Secondary::Pieces;d.completedOnly=true;add(d);
        }
        for(int lines:{10,40}) {
            d=*find("dig-40");d.id="dig-quads-"+std::to_string(lines);d.name=d.chineseName="只消四挖掘 "+std::to_string(lines)+" 行";goal(d,Progress::Garbage,lines);pure_quad(d);add(d);
        }
        for(int seconds:{3,2,1}) {
            d=base("cheese-timer-"+std::to_string(seconds),"每 "+std::to_string(seconds)+" 秒上涨一行","Dig",Progress::Seconds,1000000000);
            fixed(d,Id::Goal,0);erase(d,Id::BlitzTime);fixed(d,Id::GarbageMode,4);fixed(d,Id::GarbageInterval,double(seconds));fixed(d,Id::HoleCount,1);fixed(d,Id::CheesePattern,1);fixed(d,Id::CheeseMessiness,1.0);d.secondary=Secondary::Garbage;add(d);
        }
        for(int apm:{150,200})for(int seconds:{60,180,300}) {
            d=*find("survival-60-60");d.id="survival-"+std::to_string(apm)+"-"+std::to_string(seconds);d.name=d.chineseName=std::to_string(seconds/60)+" 分钟";d.categories={"Survival",std::to_string(apm)+" APM"};goal(d,Progress::Seconds,seconds);range(d,Id::SurvivalApm,apm,inf);add(d);
        }
        for(bool defense:{false,true})for(int tier=0;tier<3;++tier) {
            d=*find("survival-60-60");d.id=std::string(defense?"defense-":"offense-")+std::to_string(tier);d.name=d.chineseName=std::vector<std::string>{"普通","困难","极限"}[tier];d.categories={defense?"Messy defense":"Straight attack"};goal(d,Progress::Seconds,180);
            range(d,Id::SurvivalApm,std::vector<double>{60,100,150}[tier],inf);range(d,Id::GarbageInTime,0,std::vector<double>{2,1,.3}[tier]);fixed(d,Id::MessinessWithinAttack,defense?.8:0.0);fixed(d,Id::GarbageCap,8);fixed(d,Id::GarbageEntryMode,0);d.secondary=defense?Secondary::Garbage:Secondary::Attack;add(d);
        }
        for(bool hard:{false,true}) {
            d=base(hard?"marathon-200-hard":"marathon-200",hard?"20G 200 行":"200 行马拉松","Gravity and field",Progress::Lines,200);if(hard)twenty_g(d);else marathon(d);add(d);
            d=base(hard?"marathon-small-hard":"marathon-small",hard?"5×10 小场地 20G":"5×10 小场地马拉松","Gravity and field",Progress::Lines,200);fixed(d,Id::FieldWidth,5);fixed(d,Id::FieldHeight,10);if(hard)twenty_g(d);else marathon(d);add(d);
        }
        for(int tier=0;tier<4;++tier) {
            d=base("master-20g-"+std::to_string(tier),"20G 大师："+std::vector<std::string>{"入门","进阶","大师","终点"}[tier],"Gravity and field",Progress::Lines,200);
            twenty_g(d,std::vector<double>{.8,.5,.25,.12}[tier]);range(d,Id::NextCount,0,std::vector<int>{5,3,2,1}[tier]);if(tier==3)fixed(d,Id::LockResetLimit,0);add(d);
        }
        for(bool noHold:{false,true})for(int tier=0;tier<3;++tier) {
            d=base("strategy-"+std::to_string(tier)+(noHold?"-no-hold":""),"策略堆叠："+std::vector<std::string>{"简单","困难","极限"}[tier]+(noHold?"（无暂存）":""),"Gravity and field",Progress::Lines,200);
            twenty_g(d,std::vector<double>{.12,.10,.08}[tier]);range(d,Id::EntryDelay,0,std::vector<double>{1,.5,.25}[tier]);if(noHold)fixed(d,Id::HoldEnabled,0);add(d);
        }
        for(int tier=0;tier<4;++tier) {
            d=base("classic-200-"+std::to_string(tier),"高速经典："+std::vector<std::string>{"简单","困难","疯狂","极限"}[tier],"Gravity and field",Progress::Lines,200);
            classic_handling(d,std::vector<double>{.8,.3,.1,.03}[tier],std::vector<double>{.5,.3,.15,.08}[tier],tier==3?0:5);
            fixed(d,Id::RotationSystem,9);fixed(d,Id::RandomPieces,6);fixed(d,Id::HoldEnabled,0);fixed(d,Id::Ghost,0);range(d,Id::NextCount,0,1);attack_rules(d);d.secondary=Secondary::Points;
            d.description="目标 200 行，按进度、分数计分。固定经典手感：DAS 16 帧、ARR 6 帧、缓降 2 帧、进块 10 帧、消行 17 帧（60.0988 Hz）；无预移动、无 DAS 预充，禁止硬降及 180°旋转。手感、重力和锁延均须符合本档固定值，禁止暂停和撤销。";add(d);
        }
        for(int tier=0;tier<5;++tier) {
            d=base("invisible-200-"+std::to_string(tier),std::vector<std::string>{"半隐 200 行","全隐 200 行","瞬隐 200 行","瞬隐+ 200 行","周期闪现 200 行"}[tier],"Visibility and memory",Progress::Lines,200);
            visibility(d,std::vector<double>{5,1.0/3,0,0,0}[tier]);if(tier==3)fixed(d,Id::Ghost,0);
            if(tier==4){fixed(d,Id::FieldInvisible,2);range(d,Id::InvisibilityInterval,5,inf);range(d,Id::InvisibilityDuration,0,1);}
            add(d);
        }
        for(int tier=0;tier<3;++tier) {
            d=base("tsd-20-"+std::to_string(tier),"连续 TSD："+std::vector<std::string>{"基础","困难","极限"}[tier],"Clear restrictions");
            d.stopRule=[tier] {return [tier,columns=std::deque<int>{}](const Placement& p,const Result&,const Result&) mutable {
                if(!p.lines)return false;
                if(p.lines!=2 || !p.spin || p.mini || Piece::metadata(Piece::Type::from_index(p.pieceType)).familyIndex!=5)return true;
                if(tier==1 && columns.size()>=3 && std::all_of(columns.end()-3,columns.end(),[&](int c){return c==p.column;}))return true;
                if(tier==2 && std::find(columns.begin(),columns.end(),p.column)!=columns.end())return true;
                columns.push_back(p.column);if(columns.size()>4)columns.pop_front();return false;
            };};add(d);
        }
        for(bool extreme:{false,true}) {
            d=base(extreme?"pure-quads-extreme":"pure-quads","纯消四："+std::string(extreme?"极限":"普通"),"Clear restrictions",Progress::Lines,160);
            d.stopRule=[extreme] {return [extreme,last=std::optional<int>{}](const Placement& p,const Result&,const Result&) mutable {
                if(!p.lines)return false;if(p.lines!=4 || (extreme && last && *last==p.column))return true;last=p.column;return false;
            };};add(d);
        }
        d=base("singles-marathon-200","单消马拉松 200 行","Clear restrictions",Progress::Lines,200);marathon(d);
        d.stopRule=[] {return [cleared=false](const Placement& p,const Result&,const Result&) mutable {const bool bad=p.spin || (p.lines && (p.lines!=1 || cleared));cleared=p.lines>0;return bad;};};add(d);
        for(int tier=0;tier<3;++tier) {
            d=base("c4w-"+std::to_string(tier),"四列连击："+std::vector<std::string>{"普通","困难","疯狂"}[tier],"Clear restrictions",Progress::Lines,100);fixed(d,Id::FieldWidth,4);normal_gravity(d,tier?.2:.6,tier?.25:.5);
            if(tier==2){d.progress=Progress::ClearRun;d.target=1000000000;d.endless=true;d.stopRule=[] {return [started=false](const Placement& p,const Result&,const Result&) mutable {if(p.lines)started=true;return started && !p.lines;};};}
            add(d);
        }
        for(int tier=0;tier<3;++tier) {
            d=base("pc-100-"+std::to_string(tier),"100 行全清："+std::vector<std::string>{"普通","困难","疯狂"}[tier],"Perfect clears",Progress::Lines,100);
            if(tier==0)thinking(d);else normal_gravity(d,tier==1?.6:.1,tier==1?.5:.2);d.secondary=Secondary::AllClears;add(d);
        }
        for(bool hard:{false,true}) {
            d=base(hard?"pc-consecutive-hard":"pc-consecutive",hard?"连续全清：困难":"连续全清：普通","Perfect clears",Progress::AllClears,1000000000);fixed(d,Id::Goal,0);erase(d,Id::AllClearTarget);
            if(hard){normal_gravity(d,.3,.3);fixed(d,Id::HoldEnabled,0);}else thinking(d);
            d.stopRule=[] {return [cycleLines=0](const Placement& p,const Result&,const Result& after) mutable {cycleLines+=p.lines;const bool bad=cycleLines+after.stackHeight>4;if(p.allClear)cycleLines=0;return bad;};};add(d);
        }
        d=base("symmetry-40","对称 40 行","Construction");construction_hint(d,15);d.stopRule=[] {return [](const Placement& p,const Result&,const Result&){return p.lines && !p.symmetric;};};add(d);
        d=base("construct-secret","秘密段位：大于号","Construction",Progress::Pattern,19);construction_hint(d,12);fixed(d,Id::Goal,0);d.secondary=Secondary::Pieces;thinking(d);add(d);
        d=base("construct-checkerboard","棋盘格堆叠","Construction",Progress::Pattern,20);construction_hint(d,13);fixed(d,Id::Goal,0);d.secondary=Secondary::Pieces;thinking(d);add(d);
        for(int tier=0;tier<3;++tier) {
            d=base(std::vector<std::string>{"instinct-200","blind-200","audio-200"}[tier],std::vector<std::string>{"本能：20G 当前块隐形","全程盲操 200 行","仅听音 200 行"}[tier],"Visibility and memory",Progress::Lines,200);
            fixed(d,Id::ChallengeRules,tier+1);fixed(d,Id::Ghost,0);
            if(tier==2){d.testing=true;d.description="声音设备兼容性及提示辨识仍需实测。";}
            if(tier==0)twenty_g(d);else{visibility(d,0);fixed(d,Id::NextCount,0);fixed(d,Id::HoldEnabled,0);}
            add(d);
        }
        for(bool series:{false,true}) {
            d=base(Runtime::challenge_training_id(series),series?"连续全清题库（3 题）":"固定全清训练题","Perfect clears",Progress::AllClears,series?3:1);
            fixed(d,Id::ChallengeRules,series?5:4);fixed(d,Id::NextCount,0);fixed(d,Id::HoldEnabled,0);fixed(d,Id::MapGoal,0);fixed(d,Id::MapRotationSystem,-1);
            d.map=std::make_shared<const Runtime::MapDefinition>(Runtime::challenge_puzzle(0));
            thinking(d);d.secondary=Secondary::Pieces;d.completedOnly=true;add(d);
        }
        d=base("stage-master-v1","阶段任务大师","Gravity and field",Progress::Stages,4);fixed(d,Id::Goal,0);fixed(d,Id::ChallengeRules,6);twenty_g(d,.8);attack_rules(d);add(d);
        d=base("stack-100-v1","堆积：累计四行后结算","Clear restrictions",Progress::Lines,100);fixed(d,Id::ChallengeRules,7);add(d);
        for(bool research:{false,true}) {
            d=base(research?"finesse-research-100-v1":"finesse-sprint-40-v1",research?"极简操作科研 100 攻击":"极简操作竞速 40 行","Clear restrictions",research?Progress::Attack:Progress::Lines,research?100:40);
            fixed(d,Id::ChallengeRules,research?9:8);fixed(d,Id::HoldEnabled,0);fixed(d,Id::GravityInterval,1e9+5.0);fixed(d,Id::InstantGravity,0);fixed(d,Id::GravityProgression,0);
            fixed(d,Id::SoftDropFixed,1);fixed(d,Id::SoftDropFactor,.03);fixed(d,Id::DisableHardDrop,0);attack_rules(d);
            fixed(d,Id::Das,.1);fixed(d,Id::Arr,0.0);fixed(d,Id::PreMove,0);
            d.requiresB2b=research;d.stopRule=[research] {return [research](const Placement& p,const Result&,const Result&){return p.finesseFault || (research && p.b2bBroken);};};add(d);
        }
        for(bool adverse:{false,true}) {
            d=base(adverse?"drought-plus-100":"drought-100",adverse?"干旱+ 100 行":"干旱 100 行","Sprint",Progress::Lines,100);
            fixed(d,Id::RandomPieces,adverse?9:8);if(adverse)fixed(d,Id::NextCount,0);add(d);
        }
        for(int population:{49,99}) {
            d=base("bot-royale-"+std::to_string(population)+"-v1",std::to_string(population)+" 人 Bot 混战","Local battle",Progress::Rank,population-1);
            d.testing=true;
            fixed(d,Id::ChallengeRules,population==49?10:11);fixed(d,Id::Goal,0);fixed(d,Id::GarbageMode,1);attack_rules(d);
            range(d,Id::AutoplayPps,.7,inf);range(d,Id::AutoplayWidth,300,inf);
            fixed(d,Id::GarbageInTime,.3);fixed(d,Id::GarbageEntryMode,1);fixed(d,Id::GarbageRiseInterval,0.0);fixed(d,Id::GarbageCap,8);
            fixed(d,Id::MessinessWithinAttack,0.0);fixed(d,Id::HoleCount,1);fixed(d,Id::AvoidSameColumn,0);
            d.secondary=Secondary::Attack;d.description="与 "+std::to_string(population-1)+" 个独立 bot_chatgpt 同场对战；自动随机选择存活目标，按名次、攻击数排名。整局性能和难度尚待实测，Bot 实际速度受本机算力影响。";add(d);
        }
        return out;
    }();return catalog;
}
}
