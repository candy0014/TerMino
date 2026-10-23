// C++ adaptation of MinusKelvin/cold-clear. SPDX-License-Identifier: MPL-2.0
#include "core.h"
#include "tetr/rules/garbage_queue.h"
#include <algorithm>
#include <string_view>
#include <unordered_set>

namespace ColdClear {
bool Board::occupied(int x, int y) const noexcept {
    return x < 0 || x >= Width || y < 0 || y >= Rows || (rows[y] & (1u << x));
}
bool Board::can_place(const Piece::Definition& piece, Piece::Pose pose) const {
    for (const auto& cell : piece.cells(pose.rotation))
        if (occupied(pose.position.col + cell[1], Height - 1 - pose.position.row - cell[0])) return false;
    return true;
}
bool Board::try_rotate(const Piece::Definition& piece, Piece::Pose& pose,
                       Runtime::Rotation target, Piece::KickIndex& kick) const {
    // Same transition ordering as Runtime::Piece::Definition::try_rotate.
    static constexpr int transition[4][4] = {{-1,0,8,7},{1,-1,2,10},{9,3,-1,4},{6,11,5,-1}};
    const int index = transition[pose.rotation.index()][target.index()];
    if (index < 0) return false;
    const auto metadata = Piece::metadata(piece.type());
    if (!metadata.tetrioRotation && metadata.rotationSystem == Runtime::RotationSystem::Ars &&
        (metadata.familyIndex == 1 || metadata.familyIndex == 2 || metadata.familyIndex == 5)) {
        for (int row = pose.position.row-1; row <= pose.position.row+1; ++row) {
            for (int col = pose.position.col-1; col <= pose.position.col+1; ++col) {
                if (occupied(col,Height-1-row)) {
                    if (col == pose.position.col) return false;
                    row = pose.position.row+1;
                    break;
                }
            }
        }
    }
    int k = 0;
    for (const auto& offset : piece.kick_tests()[index]) {
        // Kick tables are (x, y-up), while piece cell offsets are (row, col).
        const Piece::Pose candidate{pose.position.translated(-offset[1], offset[0]), target};
        if (can_place(piece, candidate)) {
            if (k > 0 && metadata.tetrioRotation && metadata.rotationSystem == Runtime::RotationSystem::Ars &&
                (metadata.familyIndex == 1 || metadata.familyIndex == 2 || metadata.familyIndex == 5)) {
                const auto& origin = metadata.rotationOrigins[pose.rotation.index()];
                bool forbidden = false, found = false;
                for (int row = -1; row <= 1 && !found; ++row) for (int col = -1; col <= 1; ++col)
                    if (occupied(pose.position.col-origin[1]+col,Height-1-pose.position.row+origin[0]-row)) {
                        forbidden = col == 0; found = true; break;
                    }
                if (forbidden) { ++k; continue; }
            }
            pose = candidate; kick = Piece::KickIndex::from_index(k); return true;
        }
        ++k;
    }
    return false;
}
std::array<int, Width> Board::heights() const {
    std::array<int, Width> result{};
    for (int y = 0; y < Rows; ++y) {
        if (!rows[y]) continue;
        for (int x = 0; x < Width; ++x) if (rows[y] & (1u << x)) result[x] = y + 1;
    }
    return result;
}
void Board::place(const Piece::Definition& piece, Piece::Pose pose) {
    for (const auto& cell : piece.cells(pose.rotation)) {
        const int x = pose.position.col + cell[1], y = Height - 1 - pose.position.row - cell[0];
        if (x >= 0 && x < Width && y >= 0 && y < Rows) {
            rows[y] |= 1u << x;
            pieceRows[y] |= 1u << x;
        }
    }
}
void Board::clear_lines() {
    int destination = 0;
    for (int y = 0; y < Rows; ++y) if (rows[y] != Full) {
        rows[destination] = rows[y];
        pieceRows[destination++] = pieceRows[y];
    }
    std::fill(rows.begin() + destination, rows.end(), 0);
    std::fill(pieceRows.begin() + destination, pieceRows.end(), 0);
}

Piece::Pose spawn(const Input& input, const State& state, int piece) {
    const auto& meta = input.metadata[piece];
    const auto& origin = meta.rotationOrigins[meta.spawnRotation];
    Piece::Pose pose{{-2 + origin[0], 4 + origin[1]}, Runtime::Rotation::from_index(meta.spawnRotation)};
    int minimumOffset = input.pieces[piece].cells(pose.rotation).front()[0];
    for (const auto& cell : input.pieces[piece].cells(pose.rotation)) minimumOffset = std::min(minimumOffset,cell[0]);
    const int minimumOrigin = -Runtime::kHiddenRowCapacity-minimumOffset;
    while (state.previousCleared && !state.board.can_place(input.pieces[piece], pose) && pose.position.row > minimumOrigin)
        --pose.position.row;
    return pose;
}

std::vector<Move> find_moves(const Input& input, const State& state, int piece,
                            Piece::Pose start, bool hold) {
    const auto& env = input.environment;
    const auto& definition = input.pieces[piece];
    const auto canPlace = [&](Piece::Pose p) { return state.board.can_place(definition, p); };
    if(!canPlace(start))return {};
    const auto tryRotate = [&](Piece::Pose& p, Runtime::Rotation r, Piece::KickIndex& k) {
        return state.board.try_rotate(definition, p, r, k);
    };
    const bool instantGravity = env.naturalGravityEnabled && env.gravityIntervalSeconds == 0;
    const bool instantSoftDrop = env.instant_soft_drop();
    struct Entry { Engine::MovementState movement; int time; };
    std::vector<Entry> queue;
    std::vector<Move> moves;
    // Keep kick records separate: they affect host mini/full-spin classification.
    std::vector<bool> visited((Rows + 4) * 14 * 4 * 7);
    std::unordered_set<std::size_t> landed;
    auto key = [](const Engine::MovementState& s) {
        const int kick = s.lastRotation.rotated() ? std::min(s.lastRotation.kick().index() + 1, 6) : 0;
        return (((s.pose.position.row + Runtime::kHiddenRowCapacity + 2) * 14 + s.pose.position.col + 2) * 4 +
                s.pose.rotation.index()) * 7 + kick;
    };
    auto enqueue = [&](Entry entry) {
        if (!canPlace(entry.movement.pose)) return;
        const int index = key(entry.movement);
        if (index < 0 || index >= static_cast<int>(visited.size()) || visited[index]) return;
        visited[index] = true;
        queue.push_back(entry);
    };
    Engine::MovementState initial;initial.pose=start;
    if(instantGravity)Engine::apply_presentation_instant_gravity(initial,canPlace);
    enqueue({initial, hold ? 1 : 0});
    std::vector<std::pair<char, Engine::MovementScriptCommand>> commands;
    for (char code : Engine::kMovementSearchCommands) {
        const auto command = Engine::decode_movement_script_command(code);
        if (!Engine::movement_script_command_allowed(command,env.movement,env.wall_only_horizontal(),
            env.naturalGravityEnabled,instantGravity,instantSoftDrop)) continue;
        commands.emplace_back(code, command);
    }
    for (std::size_t index = 0; index < queue.size(); ++index) {
        const Entry current = queue[index];
        auto dropped = current.movement;
        Engine::apply_movement_script_command(dropped, Engine::MovementScriptCommand::HardDrop, canPlace, tryRotate);
        if (landed.insert(key(dropped)).second)
            moves.push_back({piece, dropped.pose, dropped.lastRotation, current.time, hold});
        for (auto [code, command] : commands) {
            Entry candidate = current;
            const auto effect = Engine::apply_movement_script_command(candidate.movement, command, canPlace, tryRotate,
                env.wall_only_horizontal(), env.arrSeconds == 0, env.movement, instantGravity, instantSoftDrop);
            if (!effect.changed) continue;
            // Upstream timings: taps/rotations 1 frame, DAS slide 2; descent
            // has an extra cost so tucked placements do not look instantaneous.
            candidate.time += code == 's' || code == 'V' || code == 'S' ? 2 : (code == 'l' || code == 'r' ? 2 : 1);
            enqueue(candidate);
        }
    }
    return moves;
}

Child lock_move(const Input& input, State state, Move move) {
    const auto& env = input.environment;
    const auto& piece = input.pieces[move.piece];
    const auto& metadata = input.metadata[move.piece];
    bool immobile = true;
    for (auto offset : std::array<Piece::Offset, 4>{{{1,0},{-1,0},{0,1},{0,-1}}})
        if (state.board.can_place(piece, move.pose.translated(offset[0], offset[1]))) immobile = false;
    state.board.place(piece, move.pose);
    Rules::PlacementInput placement;
    placement.mapWidth = Width; placement.mapHeight = Height;
    // Omit the leading empty hidden rows; they cannot change clears or PC.
    const auto lockedHeights = state.board.heights();
    placement.firstRow = Height - *std::max_element(lockedHeights.begin(),lockedHeights.end());
    placement.garbageStartRow = Height - state.garbageHeight;
    placement.layerStartRow = Height - env.cheeseLayerHeight;
    placement.pose = move.pose; placement.lastRotation = move.rotation;
    placement.immobile = immobile; placement.grounded = true;
    placement.spinChecker = metadata.spinCheck;
    placement.canonicalGeometry = metadata.tetrioRotation;
    placement.currentB2B = state.b2b; placement.currentCombo = state.combo;
    placement.garbageMode = static_cast<Runtime::GarbageMode>(env.garbageModel);
    placement.spinBonuses = env.spinBonuses;
    placement.attackRules = env.attackRules;
    placement.legacySpinScoring = env.legacySpinScoring;
    placement.garbageMultiplier = env.sendMultiplier;
    placement.allSpinMod = static_cast<Runtime::AllSpinMode>(env.quickPlayAllSpin);
    placement.noHoldMod = static_cast<Runtime::ModifierMode>(env.quickPlayNoHold);
    placement.piece = {metadata.familyIndex, metadata.rotationSystem};
    placement.qp.expert = static_cast<Runtime::ModifierMode>(env.quickPlayExpert);
    placement.qp.noHold = placement.noHoldMod;
    placement.qp.messy = static_cast<Runtime::ModifierMode>(env.quickPlayMessy);
    placement.qp.gravity = static_cast<Runtime::ModifierMode>(env.quickPlayGravity);
    placement.qp.allSpin = placement.allSpinMod;
    const auto occupied = [&](int row, int col) { return state.board.occupied(col, Height - 1 - row); };
    auto lock = Rules::resolve_placement(placement,
        [&](int row, int col) {
            if (!occupied(row,col)) return Runtime::BoardCell::empty();
            return state.board.pieceRows[Height-1-row] & (1u<<col)
                ? Runtime::BoardCell::piece(0) : Runtime::BoardCell::garbage();
        }, occupied);
    state.board.clear_lines();
    state.b2b = lock.chain.b2b; state.combo = lock.chain.combo;
    state.garbageHeight = std::max(0, state.garbageHeight - lock.clear.clearedGarbageLines);
    state.previousCleared = lock.clear.clearedLines != 0;
    ++state.placed;
    const auto evaluation = evaluate(input, state, move, lock);
    if (env.garbageBufferEnabled && state.previousCleared) {
        Rules::GarbageCancelInput cancel;
        cancel.attack = lock.attack.attack; cancel.perfectClearAttack = lock.attack.perfectClearAttack + lock.attack.halfClearAttack;
        cancel.placedPieces = state.placed; cancel.multiplayerEnabled = env.openerDoubleCancelEnabled;
        cancel.openerPhase = env.openerPhase; cancel.sentAttack = state.sent;
        cancel.legacyOpenerPhase = env.legacyOpenerPhase; cancel.cancellationMultiplier = env.cancellationMultiplier;
        state.sent += Rules::cancel_incoming_garbage(state.incoming, cancel, &state.cancellation).outgoingAttack;
    }
    const double step = env.secondsPerPiece > 0 ? env.secondsPerPiece : 1.0 / std::max(0.1, env.autoplayPps);
    state.time += step + (env.pieceTime ? 0 : env.entryDelaySeconds +
        (state.previousCleared ? env.lineClearDelaySeconds : 0));
    if (env.garbageBufferEnabled && !state.previousCleared) {
        const auto rise = Rules::consume_ready_garbage(state.incoming, Runtime::TimePoint::from_seconds(state.time),
            Runtime::Duration::from_seconds(env.incomingGarbageDelaySeconds), env.garbageCap);
        for (auto chunk : rise.chunks) state.rise += chunk.lines;
    }
    Child result{std::move(state), move, std::move(lock), evaluation};
    if (result.state.rise) {
        const auto heights = result.state.board.heights();
        const int highest = *std::max_element(heights.begin(), heights.end());
        const int raised = highest + result.state.rise;
        result.evaluation.value.value -= 39 * result.state.rise +
            150 * (std::max(0, raised - 10) - std::max(0, highest - 10)) +
            511 * (std::max(0, raised - 15) - std::max(0, highest - 15));
        // Do not fabricate a future hole or continue searching unchanged terrain.
    }
    return result;
}
}
