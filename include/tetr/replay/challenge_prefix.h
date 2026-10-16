#ifndef TETR_REPLAY_CHALLENGE_PREFIX_H
#define TETR_REPLAY_CHALLENGE_PREFIX_H
#include "tetr/challenges/challenges.h"
#include <functional>

namespace Replay {
enum class PrefixPreparationStatus { Ready, NeedsAnalysis, Invalid };
struct PrefixPreparation {
    PrefixPreparationStatus status=PrefixPreparationStatus::Invalid;
    std::string recording;
    std::optional<Challenges::PrefixBoundary> boundary;
};
// A future headless replay runner can supply this for old files or changed
// rules. It must inspect the entire attempt before returning its first failure.
using PrefixAnalyzer=std::function<std::optional<Challenges::PrefixBoundary>(const std::string&)>;
std::string encode_challenge_prefixes(const std::vector<Challenges::PrefixBoundary>& boundaries);
// Input/output are unhashed recording streams. Preserve the original separately
// for Save As. Missing/stale caches never permit unchecked restricted playback.
PrefixPreparation prepare_challenge_replay(const std::string& recording,const std::string& challengeId,
    std::uint32_t ruleVersion,bool restricted,const PrefixAnalyzer& analyze={});
}
#endif
