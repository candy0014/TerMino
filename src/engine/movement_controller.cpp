#include "tetr/engine/movement_controller.h"

#include <cmath>
#include <utility>
#include <limits>

namespace Engine {

MovementController::MovementController(
    MovementState& state,
    CanPlace canPlace,
    TryRotate tryRotate, ContactObserver observer)
    : state_(state), canPlace_(std::move(canPlace)), tryRotate_(std::move(tryRotate)), observer_(std::move(observer)) {}

bool MovementController::grounded() const {
    return !canPlace_(state_.pose.translated(1, 0));
}

bool MovementController::shouldLock(
    Runtime::TimePoint now,
    Runtime::Duration lockDelay,
    Runtime::GravityInterval gravityInterval,
    bool autoplay) const {
    return now - state_.lockTimer > lockDelay &&
           gravityInterval.enabled() &&
           !autoplay &&
           grounded();
}

MovementEffect MovementController::applyGravity(Runtime::TimePoint now, Runtime::GravityInterval gravityInterval) {
    MovementEffect effect;
    if (gravityInterval.is_disabled()) return effect;
    if (gravityInterval.is_instant()) {
        while (canPlace_(state_.pose.translated(1, 0))) {
            ++state_.pose.position.row;
            state_.lockTimer = now;
            state_.lastRotation = Runtime::RotationRecord::none();
            effect.changed = true;
        }
        return effect;
    }

    if (now - state_.lockTimer > gravityInterval.interval() &&
        canPlace_(state_.pose.translated(1, 0))) {
        ++state_.pose.position.row;
        state_.lockTimer = now;
        state_.lastRotation = Runtime::RotationRecord::none();
        effect.changed = true;
    }
    return effect;
}

MovementEffect MovementController::dropToFloor(
    int scorePerCell,
    bool refreshLockTimer,
    Runtime::TimePoint now) {
    MovementEffect effect;
    while (canPlace_(state_.pose.translated(1, 0))) {
        ++state_.pose.position.row;
        state_.lastRotation = Runtime::RotationRecord::none();
        if (refreshLockTimer) state_.lockTimer = now;
        effect.scoreDelta += scorePerCell;
        effect.changed = true;
    }
    return effect;
}

MovementEffect MovementController::softDropStep(Runtime::TimePoint now) {
    MovementEffect effect;
    if (!canPlace_(state_.pose.translated(1, 0))) return effect;
    ++state_.pose.position.row;
    state_.lockTimer = now;
    state_.lastRotation = Runtime::RotationRecord::none();
    effect.changed = true;
    effect.scoreDelta = 1;
    return effect;
}

void MovementController::applyGroundedReset(
    bool wasGrounded,
    Runtime::TimePoint now,
    int lockResetLimit) {
    const bool touchesGround = wasGrounded || grounded();
    if (!touchesGround) return;
    if (state_.lockResetCount < std::numeric_limits<int>::max()) ++state_.lockResetCount;
    if (lockResetLimit < 0 || state_.lockResetCount <= lockResetLimit) state_.lockTimer = now;
}

MovementEffect MovementController::moveHorizontal(
    int direction,
    Runtime::TimePoint now,
    int lockResetLimit,
    HorizontalDropMode dropMode) {
    MovementEffect effect;
    const bool wasGrounded = grounded();
    if (!canPlace_(state_.pose.translated(0, direction))) {
        if(direction && observer_)observer_(MovementContact::Wall,direction,now);
        return effect;
    }
    state_.pose.position.col += direction;
    state_.lastRotation = Runtime::RotationRecord::none();
    applyGroundedReset(wasGrounded, now, lockResetLimit);
    effect.changed = true;
    if (dropMode != HorizontalDropMode::None)
        effect.scoreDelta += dropToFloor(dropMode==HorizontalDropMode::SoftDrop ? 1 : 0,true,now).scoreDelta;
    return effect;
}

MovementEffect MovementController::moveHorizontalToWall(
    int direction,
    Runtime::TimePoint now,
    int lockResetLimit,
    HorizontalDropMode dropMode) {
    MovementEffect effect;
    if (!direction) return effect;
    const int delta = direction < 0 ? -1 : 1;
    const bool wasGrounded = grounded();
    while (canPlace_(state_.pose.translated(0, delta))) {
        state_.pose.position.col += delta;
        if (dropMode != HorizontalDropMode::None) {
            // ARR=0 follows the surface one cell at a time: a descent can
            // block the next shift before the far wall is reachable.
            effect.scoreDelta += dropToFloor(dropMode==HorizontalDropMode::SoftDrop ? 1 : 0,true,now).scoreDelta;
        }
        effect.changed = true;
    }
    if(observer_)observer_(MovementContact::Wall,delta,now);
    if (!effect.changed) return effect;
    state_.lastRotation = Runtime::RotationRecord::none();
    // One instantaneous input consumes one reset, regardless of distance.
    applyGroundedReset(wasGrounded, now, lockResetLimit);
    effect.changed = true;
    return effect;
}

MovementEffect MovementController::rotate(
    int delta,
    Runtime::TimePoint now,
    int lockResetLimit,
    bool countLockReset) {
    MovementEffect effect;
    const bool wasGrounded = grounded();
    Piece::Pose candidate = state_.pose;
    const Runtime::Rotation target = state_.pose.rotation.rotated(delta);
    Piece::KickIndex kickIndex;
    if (!tryRotate_(candidate, target, kickIndex)) return effect;

    state_.pose = candidate;
    state_.lastRotation = Runtime::RotationRecord::from_kick(kickIndex);
    if(delta && observer_)observer_(MovementContact::Rotation,delta,now);
    if (countLockReset) applyGroundedReset(wasGrounded, now, lockResetLimit);
    effect.changed = true;
    return effect;
}

} // namespace Engine
