#include "tetr/engine/movement_presentation.h"

#include <utility>

namespace Engine {

MovementScriptCommand decode_movement_script_command(char command) {
    switch (command) {
        case 'H': return MovementScriptCommand::HardDrop;
        case 'A': return MovementScriptCommand::Rotate180;
        case 'X': return MovementScriptCommand::RotateCW;
        case 'Z': return MovementScriptCommand::RotateCCW;
        case 'S': return MovementScriptCommand::SoftDropStep;
        case 's': return MovementScriptCommand::SoftDropToFloor;
        case '<': case 'L': return MovementScriptCommand::LeftStep;
        case 'l': return MovementScriptCommand::LeftToWall;
        case '>': case 'R': return MovementScriptCommand::RightStep;
        case 'r': return MovementScriptCommand::RightToWall;
        case 'B': return MovementScriptCommand::CwLeft;
        case 'C': return MovementScriptCommand::CwRight;
        case 'D': return MovementScriptCommand::CcwLeft;
        case 'E': return MovementScriptCommand::CcwRight;
        case 'F': return MovementScriptCommand::HalfLeft;
        case 'G': return MovementScriptCommand::HalfRight;
        case 'b': return MovementScriptCommand::CwLeftToWall;
        case 'c': return MovementScriptCommand::CwRightToWall;
        case 'd': return MovementScriptCommand::CcwLeftToWall;
        case 'e': return MovementScriptCommand::CcwRightToWall;
        case 'f': return MovementScriptCommand::HalfLeftToWall;
        case 'g': return MovementScriptCommand::HalfRightToWall;
        case 'V': return MovementScriptCommand::GravityStep;
        case 'W': return MovementScriptCommand::WaitForLock;
        default: return MovementScriptCommand::Invalid;
    }
}

char movement_script_command_char(MovementScriptCommand command) {
    constexpr char codes[]="?HAXZSsLlRrBCDEFGbcdefgVW";
    const auto index=static_cast<std::size_t>(command);
    return index<sizeof(codes)-1 ? codes[index] : '?';
}

std::array<MovementScriptCommand,2> movement_script_components(MovementScriptCommand command) {
    using C=MovementScriptCommand;
    const int index=static_cast<int>(command)-static_cast<int>(C::CwLeft);
    if (index<0 || index>=12) return {command,C::Invalid};
    constexpr C rotations[]{C::RotateCW,C::RotateCCW,C::Rotate180};
    const bool wall=index>=6;
    const C move=index%2 ? (wall ? C::RightToWall : C::RightStep) : (wall ? C::LeftToWall : C::LeftStep);
    return {rotations[(index%6)/2],move};
}

namespace {

bool drop_to_floor(MovementState& state, const MovementController::CanPlace& canPlace) {
    bool changed = false;
    while (canPlace(state.pose.translated(1, 0))) {
        ++state.pose.position.row;
        state.lastRotation = Runtime::RotationRecord::none();
        changed = true;
    }
    return changed;
}

bool move_step(MovementState& state, int delta, const MovementController::CanPlace& canPlace,
    bool instantGravity, const MovementScriptRecorder& record) {
    if (!canPlace(state.pose.translated(0, delta))) {
        if (record && instantGravity) record(delta<0 ? 'L' : 'R',0,0);
        return false;
    }
    state.pose.position.col += delta;
    state.lastRotation = Runtime::RotationRecord::none();
    if (instantGravity) {
        if (record) record(delta<0 ? 'L' : 'R',0,delta);
        const int row=state.pose.position.row;
        drop_to_floor(state,canPlace);
        if (record && row!=state.pose.position.row) record('s',state.pose.position.row-row,0);
    }
    return true;
}

bool move_to_wall(MovementState& state, int delta, const MovementController::CanPlace& canPlace,
    bool instantGravity, const MovementScriptRecorder& record) {
    bool changed = false;
    const int firstColumn=state.pose.position.col;
    while (canPlace(state.pose.translated(0, delta))) {
        state.pose.position.col += delta;
        state.lastRotation = Runtime::RotationRecord::none();
        changed = true;
        if (record && instantGravity) record(delta<0 ? 'L' : 'R',0,delta);
        if (instantGravity) {
            const int before=state.pose.position.row;
            drop_to_floor(state,canPlace);
            if (record && before!=state.pose.position.row) record('s',state.pose.position.row-before,0);
        }
    }
    if (record && !instantGravity) record(delta<0 ? 'l' : 'r',0,state.pose.position.col-firstColumn);
    else if (record && !changed) record(delta<0 ? 'L' : 'R',0,0);
    return changed;
}

bool rotate(
    MovementState& state,
    int delta,
    const MovementController::TryRotate& tryRotate) {
    Piece::Pose candidate = state.pose;
    const Runtime::Rotation target = state.pose.rotation.rotated(delta);
    Piece::KickIndex kickIndex;
    if (!tryRotate(candidate, target, kickIndex)) return false;
    state.pose = candidate;
    state.lastRotation = Runtime::RotationRecord::from_kick(kickIndex);
    return true;
}

} // namespace

bool movement_script_command_allowed(MovementScriptCommand command,
    const Runtime::MovementRestrictions& restrictions, bool wallOnlyHorizontal,
    bool gravityEnabled, bool instantGravity, bool instantSoftDrop) {
    using C=MovementScriptCommand;
    if (command==C::Invalid || command==C::HardDrop || command==C::WaitForLock) return false;
    const auto components=movement_script_components(command);
    if (!instantGravity && components[1]!=C::Invalid) return false;
    for (const auto part:components) {
        if ((part==C::LeftStep || part==C::LeftToWall) && !restrictions.allows_horizontal(-1)) return false;
        if ((part==C::RightStep || part==C::RightToWall) && !restrictions.allows_horizontal(1)) return false;
        if (part==C::RotateCW && !restrictions.allows_rotation(1)) return false;
        if (part==C::RotateCCW && !restrictions.allows_rotation(-1)) return false;
        if (part==C::Rotate180 && !restrictions.allows_rotation(2)) return false;
        if ((part==C::SoftDropStep || part==C::SoftDropToFloor) && restrictions.disableSoftDrop) return false;
        if (wallOnlyHorizontal && (part==C::LeftStep || part==C::RightStep)) return false;
        if (part==C::SoftDropStep && instantSoftDrop) return false;
        if (part==C::GravityStep && (!gravityEnabled || instantGravity)) return false;
    }
    return true;
}

MovementScriptEffect apply_movement_script_command(
    MovementState& state,
    MovementScriptCommand command,
    const MovementController::CanPlace& canPlace,
    const MovementController::TryRotate& tryRotate,
    bool zeroDas,
    bool zeroArr,
    const Runtime::MovementRestrictions& restrictions,
    bool instantGravity,
    bool instantSoftDrop,
    const MovementScriptRecorder& record) {
    MovementScriptEffect effect;
    const auto components=movement_script_components(command);
    if (components[1]!=MovementScriptCommand::Invalid) {
        const auto rotation=apply_movement_script_command(state,components[0],canPlace,tryRotate,
            zeroDas,zeroArr,restrictions,false,instantSoftDrop,record);
        effect=apply_movement_script_command(state,components[1],canPlace,tryRotate,
            zeroDas,zeroArr,restrictions,instantGravity,instantSoftDrop,record);
        effect.changed=effect.changed || rotation.changed;
        return effect;
    }
    const auto before=state.pose;
    bool slideRecorded=false;
    const auto settle=[&] {
        if (!instantGravity || effect.terminate) return;
        const int row=state.pose.position.row;
        effect.changed=drop_to_floor(state,canPlace) || effect.changed;
        if (record && row!=state.pose.position.row) record('s',state.pose.position.row-row,0);
    };
    if ((command == MovementScriptCommand::HardDrop && restrictions.disableHardDrop) ||
        ((command == MovementScriptCommand::SoftDropStep || command == MovementScriptCommand::SoftDropToFloor) && restrictions.disableSoftDrop) ||
        ((command == MovementScriptCommand::LeftStep || command == MovementScriptCommand::LeftToWall) && !restrictions.allows_horizontal(-1)) ||
        ((command == MovementScriptCommand::RightStep || command == MovementScriptCommand::RightToWall) && !restrictions.allows_horizontal(1)) ||
        (command == MovementScriptCommand::RotateCW && !restrictions.allows_rotation(1)) ||
        (command == MovementScriptCommand::RotateCCW && !restrictions.allows_rotation(-1)) ||
        (command == MovementScriptCommand::Rotate180 && !restrictions.allows_rotation(2))) {
        settle(); return effect;
    }
    switch (command) {
        case MovementScriptCommand::WaitForLock:
            effect.terminate = !canPlace(state.pose.translated(1, 0));
            return effect; // Waiting has no key event and no drop score.
        case MovementScriptCommand::HardDrop:
            effect.changed = drop_to_floor(state, canPlace);
            effect.terminate = true;
            break;
        case MovementScriptCommand::Rotate180:
            effect.changed = rotate(state, 2, tryRotate);
            break;
        case MovementScriptCommand::RotateCW:
            effect.changed = rotate(state, 1, tryRotate);
            break;
        case MovementScriptCommand::RotateCCW:
            effect.changed = rotate(state, -1, tryRotate);
            break;
        case MovementScriptCommand::SoftDropStep:
            if (instantSoftDrop) effect.changed=drop_to_floor(state,canPlace);
            else if (canPlace(state.pose.translated(1, 0))) {
                ++state.pose.position.row;
                state.lastRotation = Runtime::RotationRecord::none();
                effect.changed = true;
            }
            break;
        case MovementScriptCommand::GravityStep:
            if (canPlace(state.pose.translated(1,0))) {
                ++state.pose.position.row;
                state.lastRotation=Runtime::RotationRecord::none();
                effect.changed=true;
            }
            break;
        case MovementScriptCommand::SoftDropToFloor:
            effect.changed = drop_to_floor(state, canPlace);
            break;
        case MovementScriptCommand::LeftStep:
            if (zeroDas && zeroArr) { effect.changed = move_to_wall(state,-1,canPlace,instantGravity,record); slideRecorded=true; }
            else {
                effect.changed = move_step(state, -1, canPlace,instantGravity,record);
                if (zeroDas && effect.changed) move_step(state, -1, canPlace,instantGravity,record);
                slideRecorded=instantGravity;
            }
            break;
        case MovementScriptCommand::LeftToWall:
            effect.changed = move_to_wall(state,-1,canPlace,instantGravity,record); slideRecorded=true;
            break;
        case MovementScriptCommand::RightStep:
            if (zeroDas && zeroArr) { effect.changed = move_to_wall(state,1,canPlace,instantGravity,record); slideRecorded=true; }
            else {
                effect.changed = move_step(state, 1, canPlace,instantGravity,record);
                if (zeroDas && effect.changed) move_step(state, 1, canPlace,instantGravity,record);
                slideRecorded=instantGravity;
            }
            break;
        case MovementScriptCommand::RightToWall:
            effect.changed = move_to_wall(state,1,canPlace,instantGravity,record); slideRecorded=true;
            break;
        case MovementScriptCommand::Invalid:
            break;
        default: break; // Composite commands were expanded above.
    }
    if (record && !slideRecorded && command!=MovementScriptCommand::Invalid) {
        char code=movement_script_command_char(command);
        if (command==MovementScriptCommand::SoftDropStep && instantSoftDrop) code='s';
        if (command==MovementScriptCommand::GravityStep) code='S';
        if (code=='L' && before.position.col-state.pose.position.col>1) code='l';
        if (code=='R' && state.pose.position.col-before.position.col>1) code='r';
        record(code,state.pose.position.row-before.position.row,state.pose.position.col-before.position.col);
    }
    settle();
    return effect;
}

bool apply_presentation_instant_gravity(
    MovementState& state,
    const MovementController::CanPlace& canPlace) {
    return drop_to_floor(state, canPlace);
}

MovementScriptPlayback::MovementScriptPlayback(
    std::string script,
    Runtime::TimePoint startTime,
    Runtime::Duration stepInterval)
    : script_(std::move(script)), startTime_(startTime), stepInterval_(stepInterval) {}

bool MovementScriptPlayback::finished() const { return index_ >= script_.size(); }

bool MovementScriptPlayback::commandDue(Runtime::TimePoint now) const {
    if (finished()) return false;
    return now - startTime_ >= stepInterval_ * static_cast<double>(index_);
}

MovementScriptCommand MovementScriptPlayback::consume() {
    if (finished()) return MovementScriptCommand::Invalid;
    return decode_movement_script_command(script_[index_++]);
}

} // namespace Engine
