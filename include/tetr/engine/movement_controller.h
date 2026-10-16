#ifndef TETR_ENGINE_MOVEMENT_CONTROLLER_H
#define TETR_ENGINE_MOVEMENT_CONTROLLER_H

#include "tetr/runtime/gravity.h"
#include "tetr/runtime/strong_types.h"
#include "tetr/runtime/time.h"

#include <functional>

namespace Engine {

struct MovementState {
    Piece::Pose pose{{-2, 0}, Runtime::Rotation{}};
    Runtime::RotationRecord lastRotation{};
    int lockResetCount = 0;
    Runtime::TimePoint lockTimer{};
    int inputActions=0;
};

struct MovementEffect {
    bool changed = false;
    int scoreDelta = 0;
};

enum class HorizontalDropMode { None, Gravity, SoftDrop };

class MovementController {
public:
    using CanPlace = std::function<bool(Piece::Pose pose)>;
    using TryRotate = std::function<bool(Piece::Pose& pose, Runtime::Rotation targetRotation, Piece::KickIndex& kickIndex)>;

    MovementController(MovementState& state, CanPlace canPlace, TryRotate tryRotate);

    bool grounded() const;
    void recordInput() {++state_.inputActions;}
    bool shouldLock(Runtime::TimePoint now, Runtime::Duration lockDelay, Runtime::GravityInterval gravityInterval, bool autoplay) const;
    MovementEffect applyGravity(Runtime::TimePoint now, Runtime::GravityInterval gravityInterval);
    MovementEffect dropToFloor(int scorePerCell, bool refreshLockTimer, Runtime::TimePoint now);
    MovementEffect softDropStep(Runtime::TimePoint now);
    MovementEffect moveHorizontal(int direction, Runtime::TimePoint now, int lockResetLimit,
        HorizontalDropMode dropMode = HorizontalDropMode::None);
    MovementEffect moveHorizontalToWall(int direction, Runtime::TimePoint now, int lockResetLimit,
        HorizontalDropMode dropMode = HorizontalDropMode::None);
    MovementEffect rotate(int delta, Runtime::TimePoint now, int lockResetLimit, bool countLockReset = true);

private:
    MovementState& state_;
    CanPlace canPlace_;
    TryRotate tryRotate_;
    void applyGroundedReset(bool wasGrounded, Runtime::TimePoint now, int lockResetLimit);
};

} // namespace Engine
#endif
