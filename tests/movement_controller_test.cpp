#include "tetr/compat/legacy_encoding.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/movement_input_state.h"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

struct RefState {
    int row;
    int col;
    int rotation;
    int lastRotationOp;
    int lockResetCount;
    double lockTimer;
};

bool same(const RefState& a, const Engine::MovementState& b) {
    return a.row == b.pose.position.row && a.col == b.pose.position.col && a.rotation == Compat::to_legacy(b.pose.rotation) &&
           a.lastRotationOp == Compat::to_legacy(b.lastRotation) &&
           a.lockResetCount == b.lockResetCount &&
           std::fabs(a.lockTimer - b.lockTimer.seconds()) < 1e-12;
}


template<class CanPlace>
int refGravity(RefState& s, const CanPlace& canPlace, double now, double speed) {
    if (std::fabs(speed) < 1e-8) {
        int changed = 0;
        while (canPlace(s.row + 1, s.col, s.rotation)) {
            ++s.row;
            s.lockTimer = now;
            s.lastRotationOp = 0;
            changed = 1;
        }
        return changed;
    }
    if (now - s.lockTimer > speed && canPlace(s.row + 1, s.col, s.rotation)) {
        ++s.row;
        s.lockTimer = now;
        s.lastRotationOp = 0;
        return 1;
    }
    return 0;
}

template<class CanPlace>
std::pair<int,int> refDrop(RefState& s, const CanPlace& canPlace, int scorePerCell, bool refresh, double now) {
    int changed = 0, score = 0;
    while (canPlace(s.row + 1, s.col, s.rotation)) {
        ++s.row;
        s.lastRotationOp = 0;
        if (refresh) s.lockTimer = now;
        score += scorePerCell;
        changed = 1;
    }
    return {changed, score};
}

template<class CanPlace>
std::pair<int,int> refSoftDrop(RefState& s, const CanPlace& canPlace, double now) {
    if (!canPlace(s.row + 1, s.col, s.rotation)) return {0,0};
    ++s.row;
    s.lockTimer = now;
    s.lastRotationOp = 0;
    return {1,1};
}

template<class CanPlace>
int refHorizontal(RefState& s, const CanPlace& canPlace, int dir, double now, int limit) {
    int flag = !canPlace(s.row + 1, s.col, s.rotation);
    if (!canPlace(s.row, s.col + dir, s.rotation)) return 0;
    s.col += dir;
    flag |= !canPlace(s.row + 1, s.col, s.rotation);
    s.lockResetCount += flag;
    s.lastRotationOp = 0;
    if (flag && s.lockResetCount <= limit) s.lockTimer = now;
    return 1;
}

template<class CanPlace, class TryRotate>
int refRotate(RefState& s, const CanPlace& canPlace, const TryRotate& tryRotate, int delta, double now, int limit) {
    int flag = !canPlace(s.row + 1, s.col, s.rotation);
    int row = s.row, col = s.col, kick = 0;
    const int target = (s.rotation + delta + 4) % 4;
    if (!tryRotate(row, col, s.rotation, target, kick)) return 0;
    s.row = row;
    s.col = col;
    s.rotation = target;
    flag |= !canPlace(s.row + 1, s.col, s.rotation);
    s.lockResetCount += flag;
    s.lastRotationOp = kick + 1;
    if (flag && s.lockResetCount <= limit) s.lockTimer = now;
    return 1;
}

void movementRandomRegression() {
    std::mt19937 rng(0x61a5cafeu);
    for (int tc = 0; tc < 100000; ++tc) {
        const int floor = 8 + static_cast<int>(rng() % 14);
        const int left = -2 + static_cast<int>(rng() % 4);
        const int right = 6 + static_cast<int>(rng() % 7);
        std::set<std::tuple<int,int,int>> blocked;
        for (int i = 0, n = static_cast<int>(rng() % 12); i < n; ++i) {
            blocked.emplace(
                static_cast<int>(rng() % (floor + 2)),
                left + static_cast<int>(rng() % (right - left + 1)),
                static_cast<int>(rng() % 4));
        }
        auto canPlace = [&](int row, int col, int rot) {
            if (row >= floor || col < left || col > right) return false;
            return !blocked.count({row,col,rot});
        };
        auto tryRotate = [&](int& row, int& col, int, int to, int& kick) {
            const std::array<std::pair<int,int>,4> kicks{{{0,0},{0,1},{0,-1},{-1,0}}};
            for (int i = 0; i < 4; ++i) {
                const int nr = row + kicks[i].first;
                const int nc = col + kicks[i].second;
                if (canPlace(nr,nc,to)) {
                    row = nr; col = nc; kick = i; return true;
                }
            }
            return false;
        };

        Engine::MovementState state;
        state.pose.position.row = static_cast<int>(rng() % 8) - 2;
        state.pose.position.col = left + static_cast<int>(rng() % (right-left+1));
        state.pose.rotation = Runtime::Rotation{static_cast<int>(rng() % 4)};
        state.lastRotation = Compat::rotation_record_from_legacy(static_cast<int>(rng() % 6));
        state.lockResetCount = static_cast<int>(rng() % 20);
        state.lockTimer = Runtime::TimePoint::from_seconds((rng() % 10000) / 1000.0);
        while (!canPlace(state.pose.position.row,state.pose.position.col,Compat::to_legacy(state.pose.rotation))) --state.pose.position.row;
        RefState ref{state.pose.position.row,state.pose.position.col,Compat::to_legacy(state.pose.rotation),Compat::to_legacy(state.lastRotation),state.lockResetCount,state.lockTimer.seconds()};
        auto typedCanPlace = [&](Piece::Pose pose) {
            return canPlace(pose.position.row, pose.position.col, Compat::to_legacy(pose.rotation));
        };
        auto typedTryRotate = [&](Piece::Pose& pose, Runtime::Rotation target, Piece::KickIndex& kick) {
            int row=pose.position.row, col=pose.position.col, legacyKick=0;
            if(!tryRotate(row,col,Compat::to_legacy(pose.rotation),Compat::to_legacy(target),legacyKick)) return false;
            pose.position={row,col};
            pose.rotation=target;
            kick=Compat::kick_index_from_legacy(legacyKick);
            return true;
        };
        Engine::MovementController controller(state,typedCanPlace,typedTryRotate);

        const double now = state.lockTimer.seconds() + (rng() % 5000) / 1000.0;
        const Runtime::TimePoint typedNow = Runtime::TimePoint::from_seconds(now);
        const int limit = static_cast<int>(rng() % 20);
        const int op = static_cast<int>(rng() % 7);
        if (op == 0) {
            const double speed = (rng()%5==0) ? 0.0 : (rng()%1500)/1000.0;
            const int a = refGravity(ref,canPlace,now,speed);
            const auto b = controller.applyGravity(typedNow,Compat::gravity_from_legacy_seconds(speed));
            assert(a == static_cast<int>(b.changed));
        } else if (op == 1) {
            const int scorePerCell = (rng()%2) ? 2 : 0;
            const bool refresh = rng()%2;
            const auto a = refDrop(ref,canPlace,scorePerCell,refresh,now);
            const auto b = controller.dropToFloor(scorePerCell,refresh,typedNow);
            assert(a.first == static_cast<int>(b.changed));
            assert(a.second == b.scoreDelta);
        } else if (op == 2) {
            const auto a = refSoftDrop(ref,canPlace,now);
            const auto b = controller.softDropStep(typedNow);
            assert(a.first == static_cast<int>(b.changed));
            assert(a.second == b.scoreDelta);
        } else if (op == 3 || op == 4) {
            const int dir = op == 3 ? -1 : 1;
            const int a = refHorizontal(ref,canPlace,dir,now,limit);
            const auto b = controller.moveHorizontal(dir,typedNow,limit);
            assert(a == static_cast<int>(b.changed));
        } else {
            const int delta = op == 5 ? 1 : (rng()%2 ? -1 : 2);
            const int a = refRotate(ref,canPlace,tryRotate,delta,now,limit);
            const auto b = controller.rotate(delta,typedNow,limit);
            assert(a == static_cast<int>(b.changed));
        }
        assert(same(ref,state));
    }
}

void lockConditionRegression() {
    Engine::MovementState state;
    state.pose={{5,3},Runtime::Rotation{0}}; state.lockTimer = Runtime::TimePoint::from_seconds(10.0);
    auto canPlace = [](Piece::Pose pose){ return pose.position.row < 6; };
    auto tryRotate = [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&){ return false; };
    Engine::MovementController c(state,canPlace,tryRotate);
    assert(!c.shouldLock(Runtime::TimePoint::from_seconds(10.49),Runtime::Duration::from_seconds(0.5),Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(1.0)),false));
    assert(c.shouldLock(Runtime::TimePoint::from_seconds(10.51),Runtime::Duration::from_seconds(0.5),Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(1.0)),false));
    assert(!c.shouldLock(Runtime::TimePoint::from_seconds(10.51),Runtime::Duration::from_seconds(0.5),Runtime::GravityInterval::disabled(),false));
    assert(!c.shouldLock(Runtime::TimePoint::from_seconds(10.51),Runtime::Duration::from_seconds(0.5),Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(1.0)),true));
}

void inputStateRegression() {
    Engine::MovementInputState input;
    input.clear();
    std::vector<int> keys{10,11,12,13,14,15};
    std::array<int, Input::kBindableActionCount> keyCodes{};
    keyCodes.fill(-1);
    keyCodes[0]=10; // left
    keyCodes[1]=11; // right
    keyCodes[2]=12; // soft drop
    keyCodes[3]=13; // hard drop
    keyCodes[4]=14; // clockwise
    keyCodes[7]=15; // hold
    std::array<bool,256> down{};

    // Establish a carried left DAS state before spawning the next piece.
    input.setPhase(10,Engine::KeyPhase::Repeating);
    input.setTimestamp(10,Runtime::TimePoint::from_seconds(4.0));
    down[10]=true;
    down[12]=true; // soft drop intentionally does not debounce across pieces
    down[13]=true; // hard drop does
    down[14]=true; // rotations do
    const auto typedBindings=Input::BindingSet::from_key_codes(keyCodes);
    input.preparePiece(typedBindings,[&](int k){return down[k];},Runtime::TimePoint::from_seconds(5.0));
    assert(input.phase(10)==Engine::KeyPhase::Repeating && input.timestamp(10).seconds()==4.0);
    assert(input.phase(12)==Engine::KeyPhase::Released);
    assert(input.phase(13)==Engine::KeyPhase::AwaitRelease);
    assert(input.phase(14)==Engine::KeyPhase::AwaitRelease);

    assert(input.horizontalRepeatDue(10,Runtime::TimePoint::from_seconds(5.02),Runtime::Duration::from_seconds(0.1),Runtime::Duration::from_seconds(0.01)));
    assert(input.phase(10)==Engine::KeyPhase::Repeating);
    assert(input.beginPress(12,true,Runtime::TimePoint::from_seconds(5.02)));
    assert(input.softDropRepeatDue(12,Runtime::TimePoint::from_seconds(5.20),Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(0.5)),10.0));

    assert(input.consumeReleaseDebounce(13,true));
    assert(input.phase(13)==Engine::KeyPhase::AwaitRelease);
    assert(input.consumeReleaseDebounce(13,false));
    assert(input.phase(13)==Engine::KeyPhase::Released);

    input.setPhase(11,Engine::KeyPhase::Pressed);
    input.setTimestamp(11,Runtime::TimePoint::from_seconds(6.0));
    input.setPhase(10,Engine::KeyPhase::Pressed);
    input.setTimestamp(10,Runtime::TimePoint::from_seconds(5.5));
    assert(input.mostRecentActive({10,11})==11);
}

} // namespace

int main() {
    movementRandomRegression();
    lockConditionRegression();
    inputStateRegression();
    std::cout << "movement controller regression: OK\n";
    return 0;
}
