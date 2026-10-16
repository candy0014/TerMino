#ifndef TETR_CHALLENGES_CHALLENGES_H
#define TETR_CHALLENGES_CHALLENGES_H

#include "tetr/config/settings_schema.h"
#include "tetr/runtime/runtime_config.h"
#include "tetr/runtime/player_state.h"
#include <filesystem>
#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Challenges {
enum class Restriction { Any, Fixed, Set, Interval };
struct Constraint {
    UserConfig::SettingId setting;
    Restriction restriction=Restriction::Any;
    std::vector<UserConfig::PresetValue> values;
    double minimum=0,maximum=0;
    bool includeMinimum=true,includeMaximum=true;
    std::optional<UserConfig::PresetValue> preset;
    bool accepts(const UserConfig::PresetValue& value) const;
    UserConfig::PresetValue nearest(const UserConfig::PresetValue& value) const;
};
struct Failure {UserConfig::SettingId setting;std::string current,required;};
struct CheckResult {std::vector<Failure> failures;explicit operator bool() const {return failures.empty();}};
enum class Progress { Lines, Garbage, Seconds, Map, Wins, Attack, AllClears, Pattern, Stages, Rank, ClearRun };
enum class Secondary { Time, Attack, Points, Apl, Pieces, Garbage, AllClears, Combo };
struct Score {double progress=0,value=0,tie=0;};
struct Result {
    Runtime::RuntimeConfig config;
    Runtime::ScoringState statistics; // Includes the typed clear-statistics table.
    double gameSeconds=0;
    std::uint64_t undoCount=0,pauseCount=0;
    bool finished=true;
    double realSeconds=0;
    std::optional<Runtime::RuntimeConfig> opponent;
    bool goalReached=false,won=false;
    int stackHeight=0,secretGrade=0,patternRows=0;
};
// A completed lock, after its scoring and replay EndPlace have been committed.
struct Placement {
    int pieceType=0,column=0,lines=0,garbageLines=0;
    bool spin=false,mini=false,allClear=false,b2bBroken=false;
    bool symmetric=true;
    bool finesseFault=false;
};
using StopPredicate=std::function<bool(const Placement&,const Result& before,const Result& after)>;
struct Definition {
    std::string id,name;
    std::vector<std::string> categories;
    std::vector<Constraint> constraints;
    Progress progress=Progress::Lines;
    Secondary secondary=Secondary::Time;
    double target=40;
    bool noRotation=false;
    bool completedOnly=false,requiresB2b=false;
    bool endless=false;
    bool testing=false;
    // Empty for the original catalog, preserving its localized names.
    std::string chineseName,description;
    // Create fresh rule state for each attempt. True excludes this lock and
    // every subsequent lock. Bump the version whenever the rule changes.
    std::function<StopPredicate()> stopRule;
    std::uint32_t ruleVersion=1;
    std::shared_ptr<const Runtime::MapDefinition> map;
    std::vector<Constraint> opponentConstraints;
    CheckResult check(const Runtime::RuntimeConfig& config,const Runtime::RuntimeConfig* opponent=nullptr) const;
    // Menu preview resolves the same effective configuration as a fresh game.
    CheckResult check(const UserConfig::Settings& settings,const UserConfig::Settings* opponent=nullptr) const;
    std::optional<Score> val(const UserConfig::Settings& settings,const Result& result) const;
    bool change(UserConfig::Settings& settings,UserConfig::Settings* opponent=nullptr) const;
    bool better(Score first,Score second) const;
};
struct PrefixBoundary {
    std::string challengeId;
    std::uint32_t ruleVersion=1;
    std::uint64_t placements=0;
    double gameSeconds=0,realSeconds=0;
};
struct PrefixSnapshot {
    PrefixBoundary boundary;
    Result result;
    bool violated=false;
};
class PrefixTracker {
public:
    PrefixTracker()=default;
    PrefixTracker(const std::vector<Definition>& definitions,const UserConfig::Settings& settings,
                  const Result& initial,std::string selectedChallenge={});
    // Only an explicitly started challenge can request immediate termination.
    bool observe(const Placement& placement,const Result& after);
    bool empty() const noexcept {return entries_.empty();}
    const PrefixSnapshot* find(const std::string& challengeId) const;
    std::vector<PrefixBoundary> boundaries() const;
    // Apply whole-attempt undo/pause eligibility even to an earlier prefix.
    std::optional<Result> result_for(const Definition& definition,const Result& full) const;
private:
    struct Entry {PrefixSnapshot snapshot;StopPredicate violates;};
    std::vector<Entry> entries_;
    std::string selected_;
    std::uint64_t placements_=0;
};
// Stable original catalog; use available_definitions/lookup for local maps and duels.
const std::vector<Definition>& definitions();
const Definition* find(const std::string& id);
std::vector<Definition> available_definitions();
std::vector<Definition> additional_definitions();
std::optional<Definition> lookup(const std::string& id);
Runtime::RuntimeConfig preview_config(const UserConfig::Settings& settings);
std::optional<UserConfig::PresetValue> runtime_value(const Runtime::RuntimeConfig& config,UserConfig::SettingId setting);
void restore_adjustment(UserConfig::Settings& settings,UserConfig::Settings* opponent=nullptr);
void restore_replay(UserConfig::Settings& settings);
UserConfig::Settings persistent_settings(const UserConfig::Settings& settings);

struct Record {
    std::uint64_t runId=0;
    std::int64_t playedAt=0; // UTC milliseconds; displayed in local time.
    std::string challengeId;
    Score score;
    bool improved=false;
    std::string replayPath; // UTF-8 project-relative path, or absolute on another Windows drive.
};
struct Outcome {std::string challengeId;Score score;bool personalBest=false;int player=-1;};
struct Round {
    UserConfig::Settings settings;
    Runtime::RuntimeConfig config;
    std::int64_t playedAt=0;
    std::uint64_t runId=0;
    bool evaluated=false,historyWritable=true;
    std::uint64_t endedUndoCount=0,endedPauseCount=0;
    std::vector<Outcome> outcomes;
    std::string notice;
    PrefixTracker prefixes;
    std::string selectedChallenge;
    std::optional<Result> completedResult;
    // Materialized by the app; replay serialization has no config-library dependency.
    std::vector<PrefixBoundary> replayPrefixes;
    std::optional<Runtime::RuntimeConfig> opponent;
};
struct History {
    std::uint64_t nextRunId=1;
    std::vector<Record> records;
    std::optional<Record> best(const Definition& definition) const;
    std::vector<Record> recent(const Definition& definition) const;
    std::vector<Outcome> add(const UserConfig::Settings& settings,const Result& result,std::int64_t playedAt,std::uint64_t& runId,const PrefixTracker* prefixes=nullptr);
    bool attach_replay(std::uint64_t runId,const std::filesystem::path& path);
    void retain();
};
std::filesystem::path history_path(const UserConfig::Settings& settings);
bool load(History& history,const std::filesystem::path& path);
bool save(const History& history,const std::filesystem::path& path);
bool replay_exists(const Record& record);
std::int64_t utc_now_milliseconds();
} // namespace Challenges
#endif
