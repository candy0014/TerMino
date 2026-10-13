#ifndef TETR_BOT_ENVIRONMENT_H
#define TETR_BOT_ENVIRONMENT_H

#include "tetr/runtime/movement_restrictions.h"
#include "tetr/runtime/spin_bonuses.h"
#include "tetr/runtime/attack_rules.h"
#include "tetr/runtime/quick_play_progress.h"
#include "tetr/runtime/strong_types.h"
#include <array>
#include <memory>
#include <atomic>
#include "tetr/autoplay/availability.h"
namespace Runtime { struct RuntimeConfig; }

namespace Bot {

// Explicit snapshot of the host settings that the historical BOT algorithms
// actually read. Values intentionally retain their historical scalar encoding
// so strategy math can remain byte-for-byte mechanical while ownership is no
// longer process-global.
struct Environment {
    std::shared_ptr<std::atomic<bool>> searchCancelled;
    int garbageModel = 0;
    int cheeseModel = 1;
    double cheeseMessiness = 0.0;
    int cheeseLayerHeight = 6;
    double incomingGarbageDelaySeconds = 0.0;
    int quickPlayNoHold = 0;
    int quickPlayMessy = 0;
    int quickPlayAllSpin = 0;
    int quickPlayFool = 0;
    double gravityIntervalSeconds = 0.0;
    int fieldWidth = 10;
    int fieldHeight = 20;
    int nextCount = 5;
    bool holdEnabled = true;
    bool antiGravity = false;
    int rotationSystem = 0;
    bool randomPieces = false;
    double autoplayPps = 2.5;
    int autoplaySearchWidth = 300;
    int autoplayMode = 0;
    int quickPlayFloor = 1;
    double nowSeconds = 0.0;
    // Explicit timing/rule capabilities, not a scenario or opponent label.
    double secondsPerPiece = 0.0; // 0: no configured pace; BOT estimates its budget.
    bool openerDoubleCancelEnabled = false;
    int openerPhase = 14;
    bool legacyOpenerPhase = true;
    int sentAttack = 0;
    int garbageCap = 8;
    double sendMultiplier = 1.0;
    double dasSeconds = 0.1;
    double arrSeconds = 0.01;
    double softDropFactor = 0.0;
    bool softDropFixed = false;
    bool naturalGravityEnabled = true;
    bool infiniteHold = false;
    int holdCount = 1;
    Runtime::SpinBonuses spinBonuses = Runtime::SpinBonuses::AllMiniPlus;
    bool legacySpinScoring = false;
    double hitMultiplier = 1.0;
    double cancellationMultiplier = 1.0;
    bool garbageBufferEnabled = false;
    double entryDelaySeconds = 0.0;
    double lineClearDelaySeconds = 0.0;
    bool pieceTime = false;
    bool handlingSimulation = false;
    double lockDelaySeconds = 0.5;
    double dcdSeconds = 0;
    bool preMove = true;
    bool dasPrecharge = true;
    int lockResetLimit = 15;
    bool infiniteMovement = false;
    double rootPaceRemainingSeconds = 0;
    int quickPlayGravity = 0;
    int quickPlayExpert = 0;
    int quickPlayVolatile = 0;
    int quickPlayDoubleHole = 0;
    int quickPlayBackfire = 0;
    int quickPlaySnakeEyes = 0;
    int quickPlaySnowball = 0;
    // Rule snapshot and already announced progress; no future bag/garbage RNG.
    std::shared_ptr<const Runtime::RuntimeConfig> quickPlayRules;
    Runtime::QuickPlayProgress quickPlayProgress;
    double quickPlayAltitude = 0.0;
    double quickPlayBooster = 1.0;
    double elapsedGameSeconds = 0.0;
    std::array<Piece::Type,7> pieceTypes{Piece::Type{0},Piece::Type{1},Piece::Type{2},Piece::Type{3},Piece::Type{4},Piece::Type{5},Piece::Type{6}};
    Runtime::MovementRestrictions movement;
    Runtime::AttackRules attackRules;

    // Zero DAS removes the isolated tap. Strategies without repeat/charge
    // state can safely plan whole slides instead; zero ARR alone permits taps.
    bool wall_only_horizontal() const noexcept { return dasSeconds == 0.0; }
    bool instant_soft_drop() const noexcept { return softDropFactor<=0.0 || gravityIntervalSeconds==0.0; }
    bool partial_descent_available() const noexcept {
        return naturalGravityEnabled || (!movement.disableSoftDrop && !instant_soft_drop());
    }
};

Environment make_environment(const Runtime::RuntimeConfig& config);

} // namespace Bot

#endif
