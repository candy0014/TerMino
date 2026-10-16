#include "tetr/challenges/challenges.h"
#include "tetr/config/map_store.h"
#include "tetr/config/presets.h"
#include "tetr/runtime/piece.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <limits>

namespace Challenges {
namespace {
using Id=UserConfig::SettingId;
Constraint fixed(Id id,UserConfig::PresetValue value) {return {id,Restriction::Fixed,{std::move(value)}};}
void set(std::vector<Constraint>& constraints,Id id,UserConfig::PresetValue value) {
    const auto found=std::find_if(constraints.begin(),constraints.end(),[&](const auto& c){return c.setting==id;});
    if(found==constraints.end())constraints.push_back(fixed(id,std::move(value)));else *found=fixed(id,std::move(value));
}
Definition duel(const std::string& id,const std::string& name,bool turns,double speed) {
    const double pps=turns?2.0:speed;
    auto d=*find("sprint-40");d.id=id;d.name=name;d.categories={"Local battle"};d.progress=Progress::Wins;d.target=1;
    d.constraints.erase(std::remove_if(d.constraints.begin(),d.constraints.end(),[](const auto& c){return c.setting==Id::RacingDistance;}),d.constraints.end());
    set(d.constraints,Id::GameMode,6);set(d.constraints,Id::GarbageMode,11);set(d.constraints,Id::Goal,0);set(d.constraints,Id::TurnPieces,turns?1.0:0.0);
    // Pin attack/garbage rules on both sides, including opponent difficulty.
    for(const auto& c:find("survival-60-60")->constraints)
        if(c.setting!=Id::Goal && c.setting!=Id::BlitzTime && c.setting!=Id::SurvivalApm &&
           std::none_of(d.constraints.begin(),d.constraints.end(),[&](const auto& existing){return existing.setting==c.setting;}))d.constraints.push_back(c);
    d.opponentConstraints=d.constraints;
    d.categories.push_back(turns?"Turn-based vs Bot (bot_chatgpt)":"Bot duel (bot_chatgpt)");
    if(turns) {
        set(d.opponentConstraints,Id::TurnPieces,speed);
        for(auto& c:d.opponentConstraints)if(c.setting==Id::TurnPieces)c={Id::TurnPieces,Restriction::Interval,{},speed,std::numeric_limits<double>::infinity(),true,false,speed};
    }
    set(d.opponentConstraints,Id::AutoplayMode,static_cast<int>(Runtime::AutoplayMode::ChatGPT));
    set(d.opponentConstraints,Id::AutoplayPps,pps);
    for(auto& c:d.opponentConstraints)if(c.setting==Id::AutoplayPps)c={Id::AutoplayPps,Restriction::Interval,{},pps,std::numeric_limits<double>::infinity(),true,false,pps};
    set(d.opponentConstraints,Id::AutoplayWidth,300);
    for(auto& c:d.opponentConstraints)if(c.setting==Id::AutoplayWidth)c={Id::AutoplayWidth,Restriction::Interval,{},300,std::numeric_limits<double>::infinity(),true,false,300};
    set(d.opponentConstraints,Id::NextCount,5);set(d.opponentConstraints,Id::HoldEnabled,1);set(d.opponentConstraints,Id::HoldCount,1);set(d.opponentConstraints,Id::QpNoHold,0);
    const UserConfig::Settings defaults;
    for(const auto id:{Id::FieldHeight,Id::RotationSystem,Id::GravityInterval,Id::InstantGravity,Id::GravityProgression,
        Id::LockDelay,Id::LockResetLimit,Id::InfiniteMovement,Id::Das,Id::Arr,Id::Dcd,Id::SoftDropFactor,Id::SoftDropFixed,
        Id::PreMove,Id::DasPrecharge,Id::LineClearDelay,Id::EntryDelay,Id::DisableLeft,Id::DisableRight,
        Id::DisableClockwise,Id::DisableCounterclockwise,Id::Disable180,Id::DisableSoftDrop,Id::DisableHardDrop})
        set(d.opponentConstraints,id,UserConfig::Presets::read_value(defaults,id));
    set(d.opponentConstraints,Id::AutoplayHandlingSimulation,0);
    return d;
}
Definition map_challenge(const Runtime::MapDefinition& map) {
    Definition d;d.name="#"+std::to_string(map.id)+": "+map.name;d.categories={"Maps"};d.progress=Progress::Map;d.target=1;
    d.map=std::make_shared<const Runtime::MapDefinition>(map);
    auto canonical=map;canonical.name.clear();std::uint64_t hash=14695981039346656037ull;
    for(unsigned char c:Runtime::encode_map(canonical)){hash^=c;hash*=1099511628211ull;}
    std::ostringstream id;id<<"map-"<<map.id<<'-'<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;d.id=id.str();
    auto goal=map.goal;
    if(goal==Runtime::MapGoal::Disabled) {
        const bool occupied=std::any_of(map.rows.begin(),map.rows.end(),[](const auto& row){return row.find_first_not_of(" .")!=std::string::npos;});
        goal=occupied?Runtime::MapGoal::ClearInitial:Runtime::MapGoal::AllClear;
    }
    d.constraints={fixed(Id::GameMode,0),fixed(Id::MapId,map.id),fixed(Id::MapRotationSystem,-1),
        fixed(Id::MapGoal,static_cast<int>(goal)),fixed(Id::Goal,goal==Runtime::MapGoal::ClearInitial?7:6),
        fixed(Id::FieldWidth,map.width),fixed(Id::FieldHeight,map.height),
        fixed(Id::AutoplayMode,0),fixed(Id::SimulationPps,0.0),fixed(Id::TurnPieces,0.0)};
    d.constraints.push_back({Id::GravityInterval,Restriction::Interval,{},0,1});
    if(map.generator==Runtime::MapGenerator::RotationSystem)d.constraints.push_back(fixed(Id::RotationSystem,static_cast<int>(map.rotationSystem)));
    else if(!map.sequence.empty())d.constraints.push_back(fixed(Id::RotationSystem,static_cast<int>(Piece::metadata(map.sequence.front()).rotationSystem)));
    if(goal==Runtime::MapGoal::AllClear)d.constraints.push_back(fixed(Id::AllClearTarget,1));
    return d;
}
Definition construction_map_challenge(const Runtime::MapDefinition& source) {
    auto target=Runtime::construction_map_target(source);auto d=map_challenge(target);
    d.id="construct-"+d.id;d.categories={"Construction","Maps"};
    d.constraints={fixed(Id::ChallengeRules,static_cast<int>(Runtime::ChallengeRules::ConstructMap)),
        fixed(Id::GameMode,0),fixed(Id::Goal,0),fixed(Id::GarbageMode,0),fixed(Id::MapId,source.id),
        fixed(Id::FieldWidth,source.width),fixed(Id::FieldHeight,source.height),
        fixed(Id::AutoplayMode,0),fixed(Id::SimulationPps,0.0),fixed(Id::TurnPieces,0.0)};
    return d;
}
}
std::vector<Definition> available_definitions() {
    auto result=definitions();
    auto additional=additional_definitions();result.insert(result.end(),additional.begin(),additional.end());
    for(const auto& [millis,label]:std::vector<std::pair<int,std::string>>{{700,"Easy (0.7 PPS)"},{1200,"Normal (1.2 PPS)"},{1500,"Hard (1.5 PPS)"},{2000,"Crazy (2 PPS)"},{2500,"Extreme (2.5 PPS)"}}) {
        result.push_back(duel("local-bot-chatgpt-"+std::to_string(millis),label,false,millis/1000.0));
    }
    for(const auto& [millis,label]:std::vector<std::pair<int,std::string>>{{300,"Easy (0.3x)"},{500,"Normal (0.5x)"},{750,"Hard (0.75x)"},{1000,"Crazy (1x)"},{1200,"Extreme (1.2x)"}})
        result.push_back(duel("local-bot-chatgpt-turn-"+std::to_string(millis),label,true,millis/1000.0));
    UserConfig::Maps::Collection maps;
    if(UserConfig::Maps::load(maps))for(const auto& map:maps.maps) {
        result.push_back(map_challenge(map));result.push_back(construction_map_challenge(map));
    }
    return result;
}
std::optional<Definition> lookup(const std::string& id) {
    if(const auto* d=find(id))return *d;
    for(auto& d:available_definitions())if(d.id==id)return std::move(d);
    return {};
}
}
