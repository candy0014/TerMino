#include "tetr/challenges/challenges.h"
#include <utility>

namespace Challenges {
PrefixTracker::PrefixTracker(const std::vector<Definition>& definitions,const UserConfig::Settings&,
                             const Result& initial,std::string selectedChallenge):selected_(std::move(selectedChallenge)) {
    for(const auto& definition:definitions) {
        if(!definition.stopRule || !definition.check(initial.config,initial.opponent?&*initial.opponent:nullptr))continue;
        auto predicate=definition.stopRule();
        if(!predicate)continue;
        PrefixBoundary boundary{definition.id,definition.ruleVersion,0,initial.gameSeconds,initial.realSeconds};
        entries_.push_back({{std::move(boundary),initial,false},std::move(predicate)});
    }
}
bool PrefixTracker::observe(const Placement& placement,const Result& after) {
    ++placements_;
    bool stop=false;
    for(auto& entry:entries_) {
        auto& prefix=entry.snapshot;
        if(!prefix.violated) {
            auto sample=after;sample.stackHeight=after.statistics.stackHeight;sample.secretGrade=after.statistics.secretGrade;sample.patternRows=after.statistics.patternRows;
            prefix.violated=entry.violates(placement,prefix.result,sample);
            if(!prefix.violated) {
                prefix.result=std::move(sample);
                prefix.boundary.placements=placements_;
                prefix.boundary.gameSeconds=after.gameSeconds;
                prefix.boundary.realSeconds=after.realSeconds;
            }
        }
        if(prefix.violated && prefix.boundary.challengeId==selected_)stop=true;
    }
    return stop;
}
const PrefixSnapshot* PrefixTracker::find(const std::string& id) const {
    for(const auto& entry:entries_)if(entry.snapshot.boundary.challengeId==id)return &entry.snapshot;
    return nullptr;
}
std::vector<PrefixBoundary> PrefixTracker::boundaries() const {
    std::vector<PrefixBoundary> result;
    for(const auto& entry:entries_)result.push_back(entry.snapshot.boundary);
    return result;
}
std::optional<Result> PrefixTracker::result_for(const Definition& definition,const Result& full) const {
    if(!definition.stopRule)return full;
    const auto* prefix=find(definition.id);
    if(!prefix || prefix->boundary.ruleVersion!=definition.ruleVersion)return {};
    auto result=prefix->result;
    result.undoCount=full.undoCount;result.pauseCount=full.pauseCount;result.finished=full.finished;
    if(!prefix->violated){result.goalReached=full.goalReached;result.won=full.won;}
    return result;
}
}
