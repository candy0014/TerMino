#ifndef TETR_RUNTIME_CHALLENGE_RULES_H
#define TETR_RUNTIME_CHALLENGE_RULES_H
namespace Runtime {
enum class ChallengeRules : int {None,Instinct,Blind,Audio,PerfectClearPuzzle,PerfectClearSeries,StageMaster,Stack,FinesseSprint,FinesseResearch,Battle49,Battle99,ConstructSecret,ConstructCheckerboard,ConstructMap,ConstructSymmetry};
inline bool construction_rules(ChallengeRules r){return r>=ChallengeRules::ConstructSecret && r<=ChallengeRules::ConstructSymmetry;}
inline bool finesse_rules(ChallengeRules r){return r==ChallengeRules::FinesseSprint || r==ChallengeRules::FinesseResearch;}
inline int challenge_population(ChallengeRules r){return r==ChallengeRules::Battle49?49:r==ChallengeRules::Battle99?99:0;}
}
#endif
