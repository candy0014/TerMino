#ifndef TETR_RUNTIME_QUICK_PLAY_PROGRESS_H
#define TETR_RUNTIME_QUICK_PLAY_PROGRESS_H

#include <array>
#include <vector>

namespace Runtime {

struct QuickPlayWindup {
    int attack = 0;
    double due = 0.0;
};

struct QuickPlayWindupWarning {
    double start = 0.0;
    double end = 0.0;
    int segments = 0;
};

// The public speed field is the booster level. Experience and animation time
// belong to the simulation and use the project's frame delta.
struct QuickPlayProgress {
    double experience = 0.0;
    double pendingAltitude = 0.0;
    double decayProtection = 0.0;
    int upgradeFatigue = 0;
    bool upgraded = false;
    double cancellation = 0.0;
    double sinceRelief = 0.0;
    int piecesSinceRelief = 0;
    std::array<int, 2> quadColumns{{-1, -1}};
    std::array<int, 2> szColumns{{-1, -1}};
    std::array<int, 2> jlColumns{{-1, -1}};
    std::array<int, 6> szPieces{{-1, -1, -1, -1, -1, -1}};
    bool extraI5Issued = false;
    int receiveProtection = 0;
    double sinceReceived = 0.0;
    double floorTime = 0.0;
    double stallMultiplier = 1.0;
    int fatigueStage = 0;
    int fatigueGarbageLines = 0;
    double windupUntil = 0.0;
    std::vector<QuickPlayWindup> windups;
    std::vector<QuickPlayWindupWarning> windupWarnings;
    double opponentTickRemainder = 0.0;
};

} // namespace Runtime

#endif
