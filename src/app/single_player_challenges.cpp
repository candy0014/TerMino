#include "tetr/util/file_path.h"
#include "single_player_challenges.h"
#include "ui_text.h"
#include "tetr/app/application_state.h"
#include "tetr/app/settings_menu.h"
#include "tetr/config/normalization.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/construction.h"
#include "tetr/rules/game_completion.h"
#include <algorithm>
#include <ctime>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <set>
#include <map>

namespace App {
UserConfig::Settings* challenge_partner_settings(Runtime::GameContext& context) {
    auto* app=context.app_component();if(!app || !app->primaryContext || !app->localPlayerTwoContext)return nullptr;
    // Outside Local Versus, Player 2 can temporarily occupy the primary context.
    if(&context==app->primaryContext && app->singlePlayerOwner==1 && app->savedPlayerOneSettings &&
       Runtime::selected_game_mode(context.user_settings())!=Runtime::GameMode::LocalVersus)return &*app->savedPlayerOneSettings;
    return &(&context==app->localPlayerTwoContext?app->primaryContext:app->localPlayerTwoContext)->user_settings();
}
namespace {
bool chinese(){return UiText::language()==UiText::Language::Chinese;}
std::string duration(double seconds) {
    if(seconds>9e12) {std::ostringstream out;out<<seconds<<" s";return out.str();}
    const auto ms=static_cast<long long>(std::llround(seconds*1000));
    std::ostringstream out;out<<ms/60000<<':'<<std::setfill('0')<<std::setw(2)<<ms/1000%60<<'.'<<std::setw(3)<<ms%1000;return out.str();
}
std::string date(std::int64_t milliseconds) {
    const auto seconds=static_cast<std::time_t>(milliseconds/1000);std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&seconds);
#else
    localtime_r(&seconds,&local);
#endif
    std::ostringstream out;out<<std::put_time(&local,"%Y-%m-%d %H:%M:%S");return out.str();
}
std::string rules(const Challenges::Definition& d,const Runtime::RuntimeConfig& config,const Runtime::RuntimeConfig* opponent) {
    std::string text;
    const auto check=d.check(config,opponent);
    if(!check)text=UiText::tr("Requirements not met: ");
    for(const auto& f:check.failures)text+=UiText::tr(UserConfig::setting_descriptor(f.setting).label)+"="+f.current+" → "+f.required+"; ";
    if(d.progress==Challenges::Progress::Wins)return text+UiText::tr("Wins rank first by shorter time; losses rank by longer survival. No undo or pause.");
    if(d.progress==Challenges::Progress::Map)return text+UiText::tr(d.categories.front()=="Construction"?
        "Build the exact marked shape from an empty field; colors do not matter. Only the map size and terrain apply.":
        "Complete this map's goal. Shorter time is better; no undo or pause.");
    if(!d.chineseName.empty()) {
        if(d.stopRule)text+=chinese()?"首次违规前的最长有效落块前缀计分；显式开始时违规立即结束。":"Only the valid placement prefix counts; a selected challenge stops on its first violation. ";
        return text+d.description;
    }
    text+=UiText::tr("Manual play; no undo or pause. Progress first, then ");
    text+=UiText::tr(d.secondary==Challenges::Secondary::Time?"shorter time.":"higher attack.");
    return text;
}
}
std::string challenge_name(const Challenges::Definition& d,bool category) {
    std::string name=d.name;
    if(chinese() && !d.chineseName.empty())name=d.chineseName;
    else if(chinese() && d.progress!=Challenges::Progress::Map && d.progress!=Challenges::Progress::Wins) {
        const auto amount=std::to_string(static_cast<int>(d.target));
        if(d.id=="sprint-pentomino-40")name="五连块40行";
        else if(d.noRotation)name="无旋转40行";
        else if(d.progress==Challenges::Progress::Lines)name=amount+"行";
        else if(d.progress==Challenges::Progress::Garbage)name=amount+"行垃圾";
        else name=std::to_string(static_cast<int>(d.target/60))+"分钟";
    }
    if(d.progress==Challenges::Progress::Wins)name=UiText::tr(d.name);
    if(d.testing)name+=(chinese()?"【测试】":" [TEST]");
    if(category) {
        std::string prefix;for(const auto& c:d.categories)prefix+=UiText::tr(c)+" / ";
        name=prefix+name;
    }
    return name;
}
std::string challenge_score(const Challenges::Definition& d,Challenges::Score s) {
    std::string result;
    if(d.progress==Challenges::Progress::Wins)return UiText::tr(s.progress>0?"Win":"Survived")+" · "+duration(s.value);
    if(d.progress==Challenges::Progress::Rank)return "#"+std::to_string(static_cast<int>(d.target+1-s.progress))+"/"+std::to_string(int(d.target+1))+" · "+std::to_string(static_cast<long long>(s.value))+" A";
    if(d.endless) {
        result=(d.progress==Challenges::Progress::Seconds?duration(s.progress):std::to_string(static_cast<long long>(s.progress))+(d.progress==Challenges::Progress::AllClears?" PC":d.progress==Challenges::Progress::ClearRun?UiText::tr(" consecutive clears"):UiText::tr(" lines")))+" · ";
    }else if(s.progress<d.target) {
        if(d.progress==Challenges::Progress::Seconds)result=duration(s.progress)+"/"+duration(d.target);
        else result=std::to_string(static_cast<int>(s.progress))+"/"+std::to_string(static_cast<int>(d.target))+
            (d.progress==Challenges::Progress::Attack?" A":d.progress==Challenges::Progress::AllClears?" PC":d.progress==Challenges::Progress::Stages?" stages":UiText::tr(" lines"));
        result+=" · ";
    }
    using S=Challenges::Secondary;
    if(d.secondary==S::Time)return result+duration(s.value);
    std::ostringstream value;
    if(d.secondary==S::Apl)value<<std::fixed<<std::setprecision(3)<<s.value<<" APL";
    else value<<static_cast<long long>(s.value)<<
        (d.secondary==S::Attack?" A":d.secondary==S::Points?" pts":d.secondary==S::Pieces?UiText::tr(" pieces"):
         d.secondary==S::Garbage?UiText::tr(" garbage lines"):d.secondary==S::AllClears?" PC":" clears");
    return result+value.str()+" · "+duration(s.tie);
}
Settings::Model::SettingNode make_challenge_menu(UserConfig::Settings& settings,const UserConfig::Settings* opponent) {
    using Node=Settings::Model::SettingNode;using Kind=Settings::Model::NodeKind;
    Challenges::History history;const bool readable=Challenges::load(history,Challenges::history_path(settings));
    const auto config=Challenges::preview_config(settings);
    const auto other=opponent?std::optional<Runtime::RuntimeConfig>{Challenges::preview_config(*opponent)}:std::nullopt;
    Node root{"challenges","Single-player challenges",settings.obscuredColors?
        "Choose a challenge. The hint lists unmet requirements.":"Choose a challenge. White meets its requirements; gray shows the unmet requirements."};
    for(const auto& d:Challenges::available_definitions()) {
        Node* parent=&root;std::string key="challenges";
        for(const auto& category:d.categories) {
            key+="/"+category;
            auto found=std::find_if(parent->children.begin(),parent->children.end(),[&](const auto& n){return n.key==key;});
            if(found==parent->children.end()){parent->children.push_back({key,category,root.description});found=std::prev(parent->children.end());found->literalTitle=category.find(" APM")!=std::string::npos;}
            parent=&*found;
        }
        Node leaf{d.id,challenge_name(d),rules(d,config,other?&*other:nullptr)};leaf.literalTitle=true;leaf.literalDescription=true;leaf.challengeId=d.id;leaf.challengeQualified=bool(d.check(config,other?&*other:nullptr));
        leaf.mapId=d.map?d.map->id:0;
        Node records{"records","Score records",readable?"Latest 10 PB improvements, 10 other plays, and 30 other saved replays. Newest first. Enter twice to play an existing replay.":"Could not read challenge records. Existing records will not be overwritten."};
        const auto pb=history.best(d);records.summary=pb?challenge_score(d,pb->score):UiText::tr("None");
        leaf.summary=pb?challenge_score(d,pb->score):UiText::tr("No records yet");
        for(const auto& r:history.recent(d)) {
            Node row{"run-"+std::to_string(r.runId),date(r.playedAt)+"  "+challenge_score(d,r.score)+(pb && r.runId==pb->runId?"  PB":""),"Enter twice to play this replay.",Kind::ChallengeReplay};
            row.literalTitle=true;row.replayPath=r.replayPath;row.challengeId=d.id;row.confirmTwice=true;row.enabled=Challenges::replay_exists(r);
            if(row.enabled)row.title+="  Re";
            if(!row.enabled)row.description=r.replayPath.empty()?"No replay was saved for this play.":"The saved replay file no longer exists.";
            records.children.push_back(std::move(row));
        }
        if(records.children.empty()) {Node empty{"empty",readable?"No records yet":"Could not read challenge records. Existing records will not be overwritten.",{},Kind::ChallengeReplay};empty.description=empty.title;empty.enabled=false;records.children.push_back(std::move(empty));}
        leaf.children.push_back(std::move(records));
        Node change{"change","Change settings to challenge standard","Apply required parameters temporarily. Browsing keeps the challenge; cancelling restores all later edits.",Kind::ChallengeChange};change.challengeId=d.id;
        leaf.children.push_back(std::move(change));
        Node start{"start","Start this challenge","Apply the challenge settings and start. A forbidden action ends an explicitly started challenge.",Kind::ChallengeStart};
        start.challengeId=d.id;start.confirmTwice=true;leaf.children.push_back(std::move(start));
        parent->children.push_back(std::move(leaf));
    }
    std::set<std::string> recorded;for(const auto& record:history.records)recorded.insert(record.challengeId);
    const auto sortMaps=[&](auto&& self,Node& category)->void {
        if(category.key=="challenges/Maps" || category.key=="challenges/Construction/Maps")std::sort(category.children.begin(),category.children.end(),[&](const auto& a,const auto& b) {
            const bool first=recorded.count(a.challengeId),second=recorded.count(b.challengeId);return first!=second?first>second:a.mapId<b.mapId;
        });
        for(auto& child:category.children)self(self,child);
    };sortMaps(sortMaps,root);
    for(const auto& key:{std::string{"challenges/Maps"},std::string{"challenges/Construction/Maps"}}) {
        Node* parent=&root;
        if(key=="challenges/Construction/Maps")for(auto& category:root.children)if(category.key=="challenges/Construction"){parent=&category;break;}
        if(std::none_of(parent->children.begin(),parent->children.end(),[&](const auto& child){return child.key==key;})) {
            Node maps{key,"Maps","No maps available"};
            Node empty{"empty","No maps available","No maps available",Kind::ChallengeReplay};empty.enabled=false;
            maps.children.push_back(std::move(empty));parent->children.push_back(std::move(maps));
        }
    }
    if(settings.challengeAdjustment)root.children.push_back({"restore","Restore original settings","Cancel the challenge and restore all settings from before selection.",Kind::ChallengeRestore});
    return root;
}
void begin_single_player_challenges(Runtime::GameContext& context) {
    auto* app=context.app_component();if(!app)return;auto& slot=app->challenge_round(&context);slot.reset();app->challenge_observer(&context)={};
    if(!context.user_settings_component() || context.config.mode.replayMode || app->automaticLocalMatch ||
       (context.config.garbage.mode==Runtime::GarbageMode::Multiplayer && context.config.mode.mode!=Runtime::GameMode::LocalVersus))return;
    Challenges::Round round;round.settings=context.user_settings();round.config=context.config;round.playedAt=Challenges::utc_now_milliseconds();
    if(context.config.mode.mode==Runtime::GameMode::LocalVersus) {
        const auto* other=&context==app->primaryContext?app->localPlayerTwoContext:app->primaryContext;
        if(other)round.opponent=other->config;
    }
    if(const auto& adjustment=round.settings.challengeAdjustment;adjustment && adjustment->explicitlyStarted) {
        const auto definition=Challenges::lookup(adjustment->challengeId);
        if(definition && definition->check(round.config,round.opponent?&*round.opponent:nullptr))round.selectedChallenge=definition->id;
    }
    Challenges::Result initial;initial.config=round.config;initial.statistics=context.session.player.gameplay.scoring;initial.opponent=round.opponent;
    round.prefixes=Challenges::PrefixTracker(Challenges::available_definitions(),round.settings,initial,round.selectedChallenge);
    slot=std::move(round);
    app->challenge_observer(&context)=[&context](const Challenges::Placement& placement,Runtime::PlayerSessionState& player) {
        auto* app=context.app_component();if(!app || !app->challenge_round(&context) || app->challenge_round(&context)->prefixes.empty())return false;
        auto& round=*app->challenge_round(&context);
        Challenges::Result after;after.config=round.config;after.statistics=player.gameplay.scoring;after.opponent=round.opponent;
        after.gameSeconds=(Runtime::elapsed_game_time(context.session,context.now())*context.config.timeMultiplier).seconds();
        after.realSeconds=player.timing.realGameStart?(context.real_now()-*player.timing.realGameStart).seconds():after.gameSeconds;
        return round.prefixes.observe(placement,after);
    };
}
void finish_single_player_challenges(Runtime::GameContext& context,Runtime::PlayerSessionState& player,bool won) {
    auto* app=context.app_component();if(!app || !app->challenge_round(&context) || context.config.mode.replayMode ||
        (context.config.garbage.mode==Runtime::GarbageMode::Multiplayer && context.config.mode.mode!=Runtime::GameMode::LocalVersus))return;
    auto& round=*app->challenge_round(&context);const auto& replay=Replay::session(context);
    if(round.evaluated && round.endedUndoCount==replay.undoCount && round.endedPauseCount==replay.pauseCount)return;
    round.evaluated=true;round.endedUndoCount=replay.undoCount;round.endedPauseCount=replay.pauseCount;
    round.outcomes.clear();round.runId=0;round.notice.clear();
    Challenges::Result result;result.config=round.config;result.statistics=player.gameplay.scoring;
    result.gameSeconds=player.timing.completedGameTime.value_or(Runtime::Duration{}).seconds();
    result.realSeconds=player.timing.completedRealTime.value_or(Runtime::Duration{}).seconds();
    result.undoCount=replay.undoCount;result.pauseCount=replay.pauseCount;
    result.opponent=round.opponent;result.won=won;
    result.goalReached=Runtime::construction_map_matches(round.config,player.gameplay.playfield.field) ||
        (Runtime::active_goal(round.config)!=Runtime::GameGoal::None && Rules::goal_reached(round.config,result.statistics,
        player.gameplay.playfield.field.remaining_initial_cells(),Runtime::Duration::from_seconds(result.gameSeconds)));
    round.completedResult=result;
    round.prefixes.finish(result);
    round.replayPrefixes=round.prefixes.boundaries();
    Challenges::History history;const auto path=Challenges::history_path(round.settings);
    round.historyWritable=Challenges::load(history,path);
    round.outcomes=history.add(round.settings,result,round.playedAt,round.runId,&round.prefixes);
    if(!round.historyWritable) {
        for(auto& outcome:round.outcomes)outcome.personalBest=false;
        round.notice=UiText::tr("Could not read challenge records. Existing records will not be overwritten.");
    } else if(!round.outcomes.empty() && !Challenges::save(history,path))round.notice=UiText::tr("Could not save challenge records.");
    if(const auto* prefix=round.prefixes.find(round.selectedChallenge)) {
        player.gameplay.scoring=prefix->result.statistics;
        player.timing.completedGameTime=Runtime::Duration::from_seconds(prefix->result.gameSeconds);
        player.timing.completedRealTime=Runtime::Duration::from_seconds(prefix->result.realSeconds);
    }
}
bool attach_challenge_replay(Runtime::GameContext& context,const std::filesystem::path& path) {
    auto* app=context.app_component();if(!app)return false;
    if(context.config.mode.replayMode) {
        auto& replay=Replay::session(context);
        if(replay.sourcePath.empty() || !context.user_settings_component())return false;
        if(replay.challengeRecords.empty()) {
            std::set<std::filesystem::path> stores{Challenges::history_path(context.user_settings())};
            if(const auto* partner=challenge_partner_settings(context))stores.insert(Challenges::history_path(*partner));
            std::error_code error;
            const auto source=Util::FilePath::absolute(std::filesystem::u8path(replay.sourcePath),error);
            if(error)return false;
            for(const auto& store:stores) {
                Challenges::History history;if(!Challenges::load(history,store))continue;
                std::set<std::uint64_t> runs;
                for(const auto& record:history.records)if(!record.replayPath.empty()) {
                    const auto candidate=Util::FilePath::absolute(std::filesystem::u8path(record.replayPath),error);
                    if(!error && (candidate==source || std::filesystem::equivalent(candidate,source,error)))runs.insert(record.runId);
                    error.clear();
                }
                for(const auto run:runs)replay.challengeRecords.push_back({store.generic_u8string(),run});
            }
        }
        std::map<std::string,std::set<std::uint64_t>> stores;
        for(const auto& link:replay.challengeRecords)stores[link.historyPath].insert(link.runId);
        bool updated=false;
        for(const auto& [name,runs]:stores) {
            Challenges::History history;const auto store=std::filesystem::u8path(name);
            if(!Challenges::load(history,store))continue;
            bool changed=false;for(const auto run:runs)changed=history.attach_replay(run,path) || changed;
            if(changed && Challenges::save(history,store))updated=true;
        }
        return updated;
    }
    if(!app->challenge_round(&context))return false;
    auto& round=*app->challenge_round(&context);if(!round.evaluated || !round.runId || !round.historyWritable)return false;
    Challenges::History history;const auto store=Challenges::history_path(round.settings);
    return Challenges::load(history,store) && history.attach_replay(round.runId,path) && Challenges::save(history,store);
}
void restore_challenge_replay(Runtime::GameContext& context) {
    if(!context.user_settings_component() || !context.user_settings().challengeReplay)return;
    Challenges::restore_replay(context.user_settings());
    if(Runtime::selected_game_mode(context.user_settings())==Runtime::GameMode::LocalVersus)
        Settings::initialize_local_versus_player_settings(context);
}
}
