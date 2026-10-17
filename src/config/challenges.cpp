#include "tetr/util/file_path.h"
#include "tetr/challenges/challenges.h"
#include "tetr/config/presets.h"
#include "tetr/config/normalization.h"
#include "tetr/runtime/piece.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <type_traits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Challenges {
namespace {
using Id=UserConfig::SettingId;
using Value=UserConfig::PresetValue;
std::filesystem::path project_relative_replay_path(const std::filesystem::path& path) {
    if(path.empty())return {};
    const auto normalized=Util::FilePath::normalized(path);
    if(!Util::FilePath::is_absolute(normalized))return normalized;
    std::error_code error;const auto root=std::filesystem::current_path(error);
    if(error)return normalized;
    const auto relative=Util::FilePath::relative(normalized,root);
    if(relative.empty())return normalized; // A different Windows drive has no relative path.
    if(!relative.empty() && *relative.begin()!="..")return relative;
    if(std::filesystem::is_regular_file(normalized,error) && !error)return relative;
    // Older histories may name the previous computer's project directory.
    // Rebase its personal replay directory after the project has been moved.
    for(auto part=normalized.begin();part!=normalized.end();++part) {
        if(*part!=".tetr")continue;
        auto next=part;++next;
        if(next==normalized.end() || *next!="replay")continue;
        std::filesystem::path relocated;
        for(;part!=normalized.end();++part)relocated/=*part;
        error.clear();
        if(std::filesystem::is_regular_file(relocated,error) && !error)return relocated;
        break;
    }
    return relative;
}
double number(const Value& value) {
    if(const auto* integer=std::get_if<int>(&value)) return *integer;
    if(const auto* real=std::get_if<double>(&value)) return *real;
    return std::numeric_limits<double>::quiet_NaN();
}
bool equal(const Value& a,const Value& b) {
    return a==b || (std::isfinite(number(a)) && number(a)==number(b));
}
std::string text(const Value& value) {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(12);
    std::visit([&](const auto& v) {
        if constexpr(std::is_same_v<std::decay_t<decltype(v)>,std::vector<int>>) {for(int key:v)out<<key<<' ';}
        else out<<v;
    },value);return out.str();
}
std::string required(const Constraint& c) {
    if(c.restriction==Restriction::Any) return "Any";
    if(c.restriction==Restriction::Interval) return std::string(c.includeMinimum?"[":"(")+text(c.minimum)+", "+text(c.maximum)+(c.includeMaximum?"]":")");
    std::string result;
    for(const auto& value:c.values) {if(!result.empty())result+=" / ";result+=text(value);}
    return result;
}
Constraint fixed(Id id,Value value) {return {id,Restriction::Fixed,{std::move(value)}};}
Constraint set(Id id,std::vector<Value> values) {return {id,Restriction::Set,std::move(values)};}
Constraint range(Id id,double low,double high) {return {id,Restriction::Interval,{},low,high};}
std::vector<Constraint> standard() {
    std::vector<Constraint> result;
    for(const auto id:{Id::ChallengeRules,Id::GameMode,Id::GarbageMode,Id::MapId,Id::AntiGravity,Id::AutoplayMode,
        Id::RandomPieces,Id::InfiniteHold,Id::QpExpert,Id::QpMessy,Id::QpGravity,Id::QpVolatile,
        Id::QpDoubleHole,Id::QpInvisible,Id::QpAllSpin,Id::QpBackfire,Id::QpSnakeEyes,Id::QpSnowball,Id::QpFool}) result.push_back(fixed(id,0));
    result.push_back(fixed(Id::SimulationPps,0.0));result.push_back(fixed(Id::TurnPieces,0.0));
    result.push_back(fixed(Id::FieldWidth,10));result.push_back(range(Id::FieldHeight,2,20));
    result.push_back(set(Id::FieldInvisible,{0,1,2}));
    result.push_back(range(Id::HoldCount,0,1));
    result.push_back(set(Id::QpNoHold,{0,1}));
    result.push_back(range(Id::NextCount,0,5));
    result.push_back(range(Id::GravityInterval,0,1));
    result.push_back(set(Id::RotationSystem,{0,1,2,7,8,9,10,11}));
    return result;
}
void replace(std::vector<Constraint>& constraints,Constraint replacement) {
    const auto it=std::find_if(constraints.begin(),constraints.end(),[&](const auto& c){return c.setting==replacement.setting;});
    if(it==constraints.end())constraints.push_back(std::move(replacement));else *it=std::move(replacement);
}
bool valid_score(Score score) {return std::isfinite(score.progress) && std::isfinite(score.value) && std::isfinite(score.tie) && score.progress>=0 && score.value>=0 && score.tie>=0;}
Id effective_adjustment_setting(const UserConfig::Settings& settings,Id setting) {
    if(setting==Id::SoftDropFixed && UserConfig::clamped_int(settings,Id::ForceSdf))return Id::ForcedSdfFixed;
    for(const auto& forced:UserConfig::forced_handling_settings())
        if(forced.personal==setting && UserConfig::clamped_int(settings,forced.enabled))return forced.value;
    return setting;
}
}

bool Constraint::accepts(const Value& value) const {
    if(restriction==Restriction::Any) return true;
    if(restriction==Restriction::Interval) {
        const double n=number(value);
        return std::isfinite(n) && !std::isnan(minimum) && !std::isnan(maximum) &&
            (includeMinimum?n>=minimum:n>minimum) && (includeMaximum?n<=maximum:n<maximum);
    }
    return std::any_of(values.begin(),values.end(),[&](const auto& item){return equal(item,value);});
}
Value Constraint::nearest(const Value& value) const {
    if(accepts(value))return value;
    if(restriction==Restriction::Interval) {
        if(std::isnan(minimum) || std::isnan(maximum) || minimum>maximum)return value;
        const bool integer=std::holds_alternative<int>(value);
        double low=minimum,high=maximum;
        if(integer) {
            low=includeMinimum?std::ceil(low):std::floor(low)+1;
            high=includeMaximum?std::floor(high):std::ceil(high)-1;
            low=std::max(low,double(std::numeric_limits<int>::min()));
            high=std::min(high,double(std::numeric_limits<int>::max()));
        } else {
            low=includeMinimum?low:std::nextafter(low,std::numeric_limits<double>::infinity());
            high=includeMaximum?high:std::nextafter(high,-std::numeric_limits<double>::infinity());
            low=std::max(low,-std::numeric_limits<double>::max());high=std::min(high,std::numeric_limits<double>::max());
        }
        if(low>high || !std::isfinite(low) || !std::isfinite(high))return value;
        const double clamped=std::clamp(std::isfinite(number(value))?number(value):0.0,low,high);
        return integer?Value{static_cast<int>(std::round(clamped))}:Value{clamped};
    }
    return values.empty()?value:values.front();
}
CheckResult Definition::check(const Runtime::RuntimeConfig& config,const Runtime::RuntimeConfig* opponent) const {
    CheckResult result;
    for(const auto& constraint:constraints) {
        if(constraint.restriction==Restriction::Any)continue;
        const auto value=runtime_value(config,constraint.setting);
        if(!value || !constraint.accepts(*value)) result.failures.push_back({constraint.setting,value?text(*value):"Unavailable",required(constraint)});
    }
    for(const auto& constraint:opponentConstraints) {
        if(constraint.restriction==Restriction::Any)continue;
        const auto value=opponent?runtime_value(*opponent,constraint.setting):std::nullopt;
        if(!value || !constraint.accepts(*value))result.failures.push_back({constraint.setting,"Opponent: "+(value?text(*value):"Unavailable"),required(constraint)});
    }
    if(config.mode.replayMode || (opponent && opponent->mode.replayMode))result.failures.push_back({Id::GameMode,"Replay","Live play"});
    if(map) {
        bool matches=false;
        if(config.initialMap && config.initialMap->id==map->id) {
            auto expected=*map,actual=*config.initialMap;expected.name.clear();actual.name.clear();matches=actual==expected;
        }
        if(!matches || !Runtime::valid_map(*map) || (map->generator==Runtime::MapGenerator::CustomSequence && map->sequence.empty()))
            result.failures.push_back({Id::MapId,"Unavailable or changed","Matching playable map"});
    }
    return result;
}
CheckResult Definition::check(const UserConfig::Settings& settings,const UserConfig::Settings* opponent) const {
    const auto config=preview_config(settings);
    const auto other=opponent?std::optional<Runtime::RuntimeConfig>{preview_config(*opponent)}:std::nullopt;
    return check(config,other?&*other:nullptr);
}
std::optional<Score> Definition::val(const UserConfig::Settings&,const Result& result) const {
    if(!check(result.config,result.opponent?&*result.opponent:nullptr) || !result.finished || result.undoCount || result.pauseCount || !std::isfinite(result.gameSeconds) || result.gameSeconds<0) return {};
    const auto& c=result.config;const auto& s=result.statistics;
    if(c.mode.replayMode || Runtime::autoplay_enabled(c.autoplay.mode) || c.simulationPps!=0 || c.timeMultiplier!=1 ||
        s.totalLines<0 || s.totalGarbageLines<0 || s.totalGarbageLines>s.totalLines || s.placedBlocks<0 || s.totalAttack<0) return {};
    long double lines=0,spins=0;
    for(const auto& [key,counts]:s.clearStatistics) {
        if(key.first<0 || static_cast<std::size_t>(key.first)>=Piece::catalog().size() || key.second<0) return {};
        lines+=(static_cast<long double>(counts[0])+counts[1]+counts[2])*key.second;
        spins+=static_cast<long double>(counts[1])+counts[2];
    }
    if(lines!=s.totalLines || (noRotation && spins!=0)) return {};
    if(progress==Progress::Wins)return Score{result.won?1.0:0.0,result.gameSeconds};
    if(progress==Progress::Map) {
        if(!result.goalReached)return {};
        return Score{1,result.gameSeconds};
    }
    if(requiresB2b && s.b2bExtensions<1)return {};
    double achieved=0;
    switch(progress) {
    case Progress::Lines:achieved=s.totalLines;break;
    case Progress::Garbage:achieved=s.totalGarbageLines;break;
    case Progress::Seconds:achieved=result.gameSeconds;break;
    case Progress::Attack:achieved=s.totalAttack;break;
    case Progress::AllClears:achieved=s.allClearCount;break;
    case Progress::Stages:achieved=s.challengeStages;break;
    case Progress::ClearRun:achieved=s.maxCombo;break;
    case Progress::Rank: {
        const int population=Runtime::challenge_population(c.challengeRules);
        if(s.battleRank<1 || s.battleRank>population)return {};
        achieved=population-s.battleRank;break;
    }
    case Progress::Pattern:achieved=id=="construct-secret"?s.secretGrade:s.patternRows;break;
    default:break;
    }
    if(completedOnly && achieved<target)return {};
    double value=0;
    switch(secondary) {
    case Secondary::Time:value=result.gameSeconds;break;
    case Secondary::Attack:value=s.totalAttack;break;
    case Secondary::Points:value=double(s.score);break;
    case Secondary::Apl:value=s.totalLines?double(s.totalAttack)/s.totalLines:0;break;
    case Secondary::Pieces:value=s.placedBlocks;break;
    case Secondary::Garbage:value=s.totalGarbageLines;break;
    case Secondary::AllClears:value=double(s.allClearCount);break;
    case Secondary::Combo:value=s.maxCombo;break;
    }
    double tie=result.gameSeconds;
    if(progress==Progress::Pattern && secondary==Secondary::Pieces) {
        const bool secret=id=="construct-secret";value=secret?s.secretGradePieces:s.patternPieces;
        tie=(secret?s.secretGradeTime:s.patternTime).seconds();
    }
    const Score score{std::min(target,achieved),value,tie};
    return valid_score(score)?std::optional<Score>{score}:std::nullopt;
}
bool Definition::better(Score a,Score b) const {
    if(rankSecondaryFirst) {
        if(a.value!=b.value)return secondary==Secondary::Time || secondary==Secondary::Pieces?a.value<b.value:a.value>b.value;
        return a.tie<b.tie;
    }
    if(a.progress!=b.progress)return a.progress>b.progress;
    if(progress==Progress::Wins && a.progress==0)return a.value>b.value;
    if(a.value!=b.value)return secondary==Secondary::Time || secondary==Secondary::Pieces?a.value<b.value:a.value>b.value;
    if(secondary==Secondary::Time)return false;
    return a.tie<b.tie;
}
void restore_adjustment(UserConfig::Settings& settings,UserConfig::Settings* opponent) {
    if(!settings.challengeAdjustment)return;
    const auto layer=std::move(*settings.challengeAdjustment);settings.challengeAdjustment.reset();
    if(opponent && layer.paired && opponent->challengeAdjustment && opponent->challengeAdjustment->paired && opponent->challengeAdjustment->challengeId==layer.challengeId)
        restore_adjustment(*opponent);
    if(layer.snapshot)settings=*layer.snapshot;
    else for(const auto& [id,value]:layer.before) UserConfig::Presets::write_value(settings,id,value);
}
void restore_replay(UserConfig::Settings& settings) {
    if(!settings.challengeReplay)return;
    const auto previous=std::move(*settings.challengeReplay);settings.challengeReplay.reset();
    settings.gameMode=previous.gameMode;settings.replayPath=previous.replayPath;
}
UserConfig::Settings persistent_settings(const UserConfig::Settings& settings) {
    auto copy=settings;restore_replay(copy);restore_adjustment(copy);return copy;
}
bool Definition::change(UserConfig::Settings& settings,UserConfig::Settings* opponent) const {
    if(!opponentConstraints.empty() && !opponent)return false;
    auto changed=settings;auto other=opponent?std::optional<UserConfig::Settings>{*opponent}:std::nullopt;
    restore_replay(changed);restore_adjustment(changed,other?&*other:nullptr);
    if(other && (!opponentConstraints.empty() || (other->challengeAdjustment && other->challengeAdjustment->paired)))restore_adjustment(*other,&changed);
    UserConfig::Presets::record_edits(changed);
    UserConfig::ChallengeAdjustment layer;layer.challengeId=id;layer.snapshot=std::make_shared<const UserConfig::Settings>(changed);
    for(const auto& constraint:constraints) {
        const auto setting=effective_adjustment_setting(changed,constraint.setting);
        const auto before=UserConfig::Presets::read_value(changed,setting);
        if(constraint.preset?equal(before,*constraint.preset):constraint.accepts(before))continue;
        layer.before.emplace(setting,before);
        UserConfig::Presets::write_value(changed,setting,constraint.preset.value_or(constraint.nearest(before)));
    }
    if(!opponentConstraints.empty()) {
        UserConfig::Presets::record_edits(*other);
        UserConfig::ChallengeAdjustment partner;partner.challengeId=id;partner.paired=true;
        partner.snapshot=std::make_shared<const UserConfig::Settings>(*other);layer.paired=true;
        for(const auto& constraint:opponentConstraints) {
            const auto setting=effective_adjustment_setting(*other,constraint.setting);
            const auto before=UserConfig::Presets::read_value(*other,setting);
            if(constraint.preset?equal(before,*constraint.preset):constraint.accepts(before))continue;
            partner.before.emplace(setting,before);
            UserConfig::Presets::write_value(*other,setting,constraint.preset.value_or(constraint.nearest(before)));
        }
        other->challengeAdjustment=std::move(partner);
    }
    if(!check(changed,other?&*other:nullptr))return false;
    changed.challengeAdjustment=std::move(layer);settings=std::move(changed);
    if(opponent && other)*opponent=std::move(*other);
    return true;
}
const std::vector<Definition>& definitions() {
    static const auto all=[] {
        std::vector<Definition> result;
        for(int goal:{10,20,40,100,1000}) {
            Definition d;d.id="sprint-"+std::to_string(goal);d.name=std::to_string(goal)+" lines";d.categories={"Sprint"};
            d.constraints=standard();d.target=goal;d.constraints.push_back(fixed(Id::Goal,2));d.constraints.push_back(fixed(Id::RacingDistance,goal));
            result.push_back(std::move(d));
        }
        auto pentomino=result[2];pentomino.id="sprint-pentomino-40";pentomino.name="Pentomino 40 lines";
        replace(pentomino.constraints,fixed(Id::RotationSystem,3));result.push_back(std::move(pentomino));
        auto noRotation=result[2];noRotation.id="sprint-no-rotation-40";noRotation.name="No rotation 40 lines";noRotation.noRotation=true;
        for(const auto id:{Id::DisableClockwise,Id::DisableCounterclockwise,Id::Disable180}) noRotation.constraints.push_back(fixed(id,1));
        result.push_back(std::move(noRotation));
        for(int goal:{10,20,40,100}) {
            Definition d;d.id="dig-"+std::to_string(goal);d.name=std::to_string(goal)+" garbage lines";d.categories={"Dig"};
            d.constraints=standard();d.progress=Progress::Garbage;d.target=goal;
            replace(d.constraints,fixed(Id::GarbageMode,3));d.constraints.push_back(fixed(Id::Goal,3));d.constraints.push_back(fixed(Id::GarbageTarget,goal));
            d.constraints.push_back(fixed(Id::CheesePattern,1));d.constraints.push_back(fixed(Id::HoleCount,1));d.constraints.push_back(fixed(Id::LayerHeight,10));
            d.constraints.push_back(fixed(Id::CheeseMessiness,1.0));
            result.push_back(std::move(d));
        }
        for(int apm:{40,60,100}) for(int seconds:{60,180,300}) {
            Definition d;d.id="survival-"+std::to_string(apm)+"-"+std::to_string(seconds);d.name=std::to_string(seconds/60)+" min";
            d.categories={"Survival",std::to_string(apm)+" APM"};d.constraints=standard();d.progress=Progress::Seconds;d.secondary=Secondary::Attack;d.target=seconds;
            const UserConfig::Settings defaults;
            for(const auto id:{Id::SpinBonuses,Id::B2BReward,Id::B2BRewardAmount,Id::B2BCharging,Id::B2BChargeThreshold,Id::B2BChargeReduction,
                Id::PerfectClearEnabled,Id::PerfectClearAttack,Id::PerfectClearCharge,Id::HalfClearEnabled,Id::HalfClearAttack,Id::HalfClearCharge,
                Id::GarbageSpecialAttack,Id::HitMultiplier,Id::CancellationMultiplier,Id::AttackMultiplier,Id::GarbageInTime,Id::GarbageEntryMode,
                Id::GarbageRiseInterval,Id::MessinessWithinAttack,Id::AvoidSameColumn,Id::GarbageCap,Id::OpenerPhase})
                d.constraints.push_back(fixed(id,UserConfig::Presets::read_value(defaults,id)));
            replace(d.constraints,fixed(Id::GarbageMode,5));d.constraints.push_back(fixed(Id::Goal,1));d.constraints.push_back(fixed(Id::BlitzTime,double(seconds)));
            auto pressure=range(Id::SurvivalApm,apm,std::numeric_limits<double>::infinity());pressure.preset=double(apm);d.constraints.push_back(std::move(pressure));result.push_back(std::move(d));
        }
        return result;
    }();return all;
}
const Definition* find(const std::string& id) {
    const auto& all=definitions();const auto it=std::find_if(all.begin(),all.end(),[&](const auto& d){return d.id==id;});return it==all.end()?nullptr:&*it;
}

std::optional<Record> History::best(const Definition& definition) const {
    std::optional<Record> result;
    for(const auto& record:records) if(record.challengeId==definition.id && (!result || definition.better(record.score,result->score) ||
        (!definition.better(result->score,record.score) && record.runId<result->runId)))result=record;
    return result;
}
std::vector<Record> History::recent(const Definition& definition) const {
    std::vector<Record> result;for(const auto& r:records)if(r.challengeId==definition.id)result.push_back(r);
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.playedAt>b.playedAt || (a.playedAt==b.playedAt && a.runId>b.runId);});return result;
}
std::vector<Outcome> History::add(const UserConfig::Settings& settings,const Result& result,std::int64_t playedAt,std::uint64_t& runId,const PrefixTracker* prefixes) {
    std::vector<Outcome> outcomes;runId=0;
    for(const auto& d:available_definitions()) {
        // Never evaluate a restricted challenge against an unchecked full game.
        std::optional<Result> candidate;
        if(prefixes)candidate=prefixes->result_for(d,result);
        else if(!d.stopRule)candidate=result;
        if(!candidate)continue;
        if(const auto score=d.val(settings,*candidate)) {
            if(!runId) {if(nextRunId==std::numeric_limits<std::uint64_t>::max())return {};runId=nextRunId++;}
            const auto pb=best(d);const bool improved=!pb || d.better(*score,pb->score);
            records.push_back({runId,playedAt,d.id,*score,improved,{}});outcomes.push_back({d.id,*score,improved});
        }
    }
    retain();return outcomes;
}
void History::retain() {
    std::vector<Record> kept;
    std::set<std::string> challenges;for(const auto& r:records)challenges.insert(r.challengeId);
    for(const auto& id:challenges) {
        std::vector<Record> rows;for(const auto& r:records)if(r.challengeId==id)rows.push_back(r);
        std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.playedAt>b.playedAt || (a.playedAt==b.playedAt && a.runId>b.runId);});
        std::set<std::uint64_t> selected;int count=0;
        for(const auto& r:rows)if(r.improved && count<10){selected.insert(r.runId);++count;}
        count=0;for(const auto& r:rows)if(!selected.count(r.runId) && count<10){selected.insert(r.runId);++count;}
        count=0;for(const auto& r:rows)if(!selected.count(r.runId) && !r.replayPath.empty() && count<30){selected.insert(r.runId);++count;}
        for(const auto& r:rows)if(selected.count(r.runId))kept.push_back(r);
    }
    records=std::move(kept);
}
bool History::attach_replay(std::uint64_t runId,const std::filesystem::path& path) {
    std::error_code error;const auto absolute=Util::FilePath::absolute(path,error);
    if(error || !std::filesystem::is_regular_file(absolute,error) || error)return false;
    const auto root=std::filesystem::current_path(error);if(error)return false;
    const auto relative=Util::FilePath::relative(absolute,root);
    const auto reference=relative.empty()?absolute:relative;
    bool changed=false;for(auto& r:records)if(r.runId==runId){r.replayPath=Util::FilePath::generic(reference);changed=true;}
    retain();return changed;
}
std::filesystem::path history_path(const UserConfig::Settings& settings) {
    return std::filesystem::u8path(settings.persistencePath.empty()?UserConfig::kDefaultUserConfigPath:settings.persistencePath).concat(".challenges");
}
bool replay_exists(const Record& record) {std::error_code error;return !record.replayPath.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(record.replayPath),error) && !error;}
std::int64_t utc_now_milliseconds() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
bool load(History& history,const std::filesystem::path& path) {
    std::error_code error;if(!std::filesystem::exists(path,error)){if(error)return false;history={};return true;}
    if(std::filesystem::file_size(path,error)>16000000 || error)return false;
    std::ifstream input(path);input.imbue(std::locale::classic());std::string magic;int version=0;std::size_t count=0;History parsed;
    if(!(input>>magic>>version>>parsed.nextRunId>>count) || magic!="TETR_CHALLENGES" || (version!=1 && version!=2) || !parsed.nextRunId || count>50000)return false;
    std::set<std::pair<std::uint64_t,std::string>> seen;
    for(std::size_t i=0;i<count;++i) {
        Record r;int improved=0;
        if(!(input>>r.runId>>r.playedAt>>std::quoted(r.challengeId)>>r.score.progress>>r.score.value))return false;
        if(version==2 && !(input>>r.score.tie))return false;
        if(!(input>>improved>>std::quoted(r.replayPath)) ||
            !r.runId || r.runId>=parsed.nextRunId || r.playedAt<0 || r.challengeId.empty() || r.challengeId.size()>128 ||
            r.replayPath.size()>4096 || !valid_score(r.score) || (improved!=0 && improved!=1) || !seen.emplace(r.runId,r.challengeId).second)return false;
        const auto replay=project_relative_replay_path(std::filesystem::u8path(r.replayPath));
        if(!r.replayPath.empty() && replay.empty())return false;
        r.replayPath=Util::FilePath::generic(replay);
        r.improved=improved;parsed.records.push_back(std::move(r));
        const auto& row=parsed.records.back();
        if(const auto* definition=find(row.challengeId);definition && (row.score.progress>definition->target ||
            (definition->secondary==Secondary::Attack && row.score.value>std::numeric_limits<int>::max())))return false;
    }
    input>>std::ws;if(!input.eof() || input.bad())return false;
    parsed.retain();history=std::move(parsed);return true;
}
bool save(const History& history,const std::filesystem::path& path) {
    if(!history.nextRunId || history.records.size()>50000)return false;
    std::set<std::pair<std::uint64_t,std::string>> seen;
    std::vector<std::string> replayPaths;replayPaths.reserve(history.records.size());
    for(const auto& r:history.records) {
        if(!r.runId || r.runId>=history.nextRunId || r.playedAt<0 || r.challengeId.empty() || r.challengeId.size()>128 ||
            r.replayPath.size()>4096 || !valid_score(r.score) || !seen.emplace(r.runId,r.challengeId).second)return false;
        if(const auto* definition=find(r.challengeId);definition && (r.score.progress>definition->target ||
            (definition->secondary==Secondary::Attack && r.score.value>std::numeric_limits<int>::max())))return false;
        const auto replay=project_relative_replay_path(std::filesystem::u8path(r.replayPath));
        if(!r.replayPath.empty() && replay.empty())return false;
        replayPaths.push_back(Util::FilePath::generic(replay));
    }
    std::error_code error;if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path(),error);if(error)return false;
    auto temporary=path;temporary+=".tmp";std::ofstream output(temporary,std::ios::trunc);if(!output)return false;
    output.imbue(std::locale::classic());output<<std::setprecision(17)<<"TETR_CHALLENGES 2 "<<history.nextRunId<<' '<<history.records.size()<<'\n';
    for(std::size_t i=0;i<history.records.size();++i) {
        const auto& r=history.records[i];
        output<<r.runId<<' '<<r.playedAt<<' '<<std::quoted(r.challengeId)<<' '<<r.score.progress<<' '<<r.score.value<<' '<<r.score.tie<<' '<<r.improved<<' '<<std::quoted(replayPaths[i])<<'\n';
    }
    output.close();if(!output)return false;
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temporary,path,error);return !error;
#endif
}
} // namespace Challenges
