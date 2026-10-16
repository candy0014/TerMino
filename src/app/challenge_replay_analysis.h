#ifndef TETR_APP_CHALLENGE_REPLAY_ANALYSIS_H
#define TETR_APP_CHALLENGE_REPLAY_ANALYSIS_H
#include "tetr/challenges/challenges.h"
namespace App {
std::optional<Challenges::PrefixBoundary> analyze_challenge_replay(const std::string& recording,const Challenges::Definition& definition);
}
#endif
