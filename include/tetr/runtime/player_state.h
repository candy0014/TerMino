#ifndef TETR_RUNTIME_PLAYER_STATE_H
#define TETR_RUNTIME_PLAYER_STATE_H

#include "tetr/runtime/playfield.h"
#include "tetr/runtime/strong_types.h"
#include "tetr/runtime/quick_play_operation.h"

#include <optional>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace Runtime {

// Counts of clear operations: normal, Spin, Mini Spin. Keys retain the actual
// catalog type and unrestricted line count (including map-opening clears).
using ClearCounts = std::array<std::uint64_t,3>;

struct PlayfieldState {
    int width = 0;
    int visibleHeight = 0;
    int activeHeight = 0;
    int nextCount = 0;
    bool holdEnabled = false;
    int garbageHeight = 0;
    int solidGarbageHeight = 0;
    Runtime::Playfield field;
};

struct ScoringState {
    bool mapAllClearReached=false;
    int placedBlocks = 0;
    int totalAttack = 0;
    int totalLines = 0;
    int totalGarbageLines = 0;
    long long score = 0;
    int wins = 0;
    int combo = 0;
    int b2b = 0;

    // Per-round combat diagnostics. These counters are derived from resolved
    // placements and do not participate in gameplay decisions or serialization.
    // Local Versus batch runs use them to compare B2B continuity, burst output,
    // digging progress and attack conversion for every BOT under the same rules.
    int difficultClears = 0;
    int b2bExtensions = 0;
    int b2bBreaks = 0;
    int maxB2B = 0;
    int maxCombo = 0;
    int clearMoves = 0;
    int zeroAttackClears = 0;
    int attacksAtLeast4 = 0;
    int attacksAtLeast8 = 0;
    int maxAttack = 0;
    int maxSpike = 0;
    int totalOutgoingAttack = 0;
    std::uint64_t totalHolds = 0;
    std::uint64_t allClearCount = 0;
    std::uint64_t totalReceivedAttack = 0;
    std::map<std::pair<int,int>,ClearCounts> clearStatistics;
    std::set<int> roundPieceTypes;
    int stackHeight=0,secretGrade=0,patternRows=0;
    int secretGradePieces=0,patternPieces=0;
    Duration secretGradeTime{},patternTime{};
    int challengeStages=0;
    int battleRank=0;
};

struct HoldState {
    Piece::Type piece = Piece::Type::none();
    int used = 0;
    std::optional<bool> remoteAvailable;
};

struct QuickPlayPlacementState {
    bool blighted = false;
    std::optional<QuickPlayClearOperation> lastOperation;
    std::vector<ExtendedAllSpinSignature> operations;
};

// Pure per-player gameplay state.  It owns only state that participates in
// the game simulation; queue/RNG/network/timing/presentation live beside it
// in PlayerSessionState.
struct PlayerState {
    struct ChallengeState {int stage=0,stageLines=0,stageQuads=0,stageSpins=0;};
    ChallengeState challenge;
    PlayfieldState playfield;
    ScoringState scoring;
    HoldState hold;
    QuickPlayPlacementState quickPlay;
};

} // namespace Runtime

#endif
