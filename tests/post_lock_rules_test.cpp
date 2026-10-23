#include "tetr/compat/legacy_encoding.h"
#include "tetr/engine/field_mutation.h"
#include "tetr/rules/line_clear.h"
#include "tetr/rules/qp_post_lock.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <random>
#include <set>
#include <vector>

namespace TetrTests::post_lock_rules_test {
namespace {


int encode_qp_legacy(const Rules::QPClearOperation& op) {
    return Compat::to_legacy(op.pieceType) | (op.linesCleared << 8) |
           (static_cast<int>(op.spin) << 11) | (static_cast<int>(op.mini) << 12);
}
Rules::QPClearOperation decode_qp_legacy(int encoded) {
    return {Compat::piece_type_from_legacy(encoded & 255), (encoded >> 8) & 7,
            ((encoded >> 11) & 1) != 0, ((encoded >> 12) & 1) != 0};
}
int encode_extended_legacy(const Rules::QPClearOperation& op) {
    return Compat::to_legacy(op.pieceType) | (op.linesCleared << 7) |
           (static_cast<int>(op.spin) << 10) | (static_cast<int>(op.mini) << 11);
}
Runtime::ExtendedAllSpinSignature decode_extended_signature_legacy(int encoded) {
    return {encoded & 127, (encoded >> 7) & 7, ((encoded >> 10) & 1) != 0,
            ((encoded >> 11) & 1) != 0, encoded >> 12};
}
int encode_extended_signature_legacy(const Runtime::ExtendedAllSpinSignature& signature) {
    return signature.pieceClass | (signature.lineClass << 7) |
           (static_cast<int>(signature.spinClass) << 10) |
           (static_cast<int>(signature.miniClass) << 11) |
           (signature.upperPieceClass << 12);
}
Rules::DoubleHoleReverseResult legacy_double_hole(
    bool enabled, int attack, int lines, int garbageLines, bool blighted) {
    if (enabled) {
        if (!garbageLines) {
            if (lines && blighted) blighted = false, attack = attack * 7 / 4 + 1;
            else attack = 0;
        } else {
            if (blighted) attack = attack * 7 / 4;
            blighted = true;
        }
    }
    return {attack, blighted};
}

Rules::SnakeEyesResult legacy_snake(int mode, int lines, int attack) {
    Rules::SnakeEyesResult out{attack, 0};
    if (mode == 2 && lines != 1 && attack) out.selfGarbage = attack;
    if (mode && lines != 1) out.attack = 0;
    return out;
}

Rules::AllSpinRepeatResult legacy_repeat(int mode, int previous, const Rules::QPClearOperation& op) {
    Rules::AllSpinRepeatResult out;
    if (previous != 8191) out.operation = decode_qp_legacy(previous);
    if (!(op.linesCleared || op.spin)) return out;
    const int res = Compat::to_legacy(op.pieceType) | (op.linesCleared << 8) |
                    (static_cast<int>(op.spin) << 11) |
                    (static_cast<int>(op.mini) << 12);
    out.operation = op;
    if (mode == 1) {
        if (res == previous) out.solidGarbage = 1;
        else if (!(res >> 11 & 1) && (res | 255) == (previous | 255)) out.solidGarbage = 1;
        else if (op.linesCleared) out.decayTimedSolidGarbage = true;
    }
    if (mode == 2) {
        const int r = op.spin ? op.linesCleared : -1;
        const int l = (previous >> 11 & 1) ? (previous >> 8 & 7) : -1;
        if (l == r) out.solidGarbage = 20;
    }
    return out;
}

void legacy_history(std::vector<int>& history, int normalized, double& multiplier) {
    int p = static_cast<int>(history.size());
    for (int i = 0; i < p; ++i) {
        if (history[i] == normalized) { p = i; break; }
    }
    if (p == static_cast<int>(history.size())) {
        history.insert(history.begin(), normalized);
    } else {
        multiplier = std::pow(0.9, 1.0 / (p + 0.5));
        for (int i = p - 1; i >= 0; --i) history[i + 1] = history[i];
        history[0] = normalized;
    }
}

void test_qp_equivalence() {
    std::mt19937 rng(20260831);
    for (int i = 0; i < 100000; ++i) {
        Rules::QPClearOperation op;
        op.pieceType = Compat::piece_type_from_legacy(static_cast<int>(rng() % 256));
        op.linesCleared = rng() % 8;
        op.spin = rng() & 1;
        op.mini = rng() & 1;

        const int encoded = encode_qp_legacy(op);
        const auto decoded = decode_qp_legacy(encoded);
        assert(decoded.pieceType == op.pieceType);
        assert(decoded.linesCleared == op.linesCleared);
        assert(decoded.spin == op.spin);
        assert(decoded.mini == op.mini);

        const int attack = rng() % 30;
        const int lines = rng() % 8;
        const int garbage = rng() % 8;
        const bool blighted = rng() & 1;
        const bool enabled = rng() & 1;
        const auto a = Rules::apply_double_hole_reverse({enabled, attack, lines, garbage, blighted});
        const auto b = legacy_double_hole(enabled, attack, lines, garbage, blighted);
        assert(a.attack == b.attack && a.blighted == b.blighted);

        const int snakeMode = rng() % 3;
        const auto snA = Rules::apply_snake_eyes(
            Compat::modifier_mode_from_legacy(snakeMode), lines, attack);
        const auto snB = legacy_snake(snakeMode, lines, attack);
        assert(snA.attack == snB.attack && snA.selfGarbage == snB.selfGarbage);

        const int backfire = rng() % 3;
        const int volatileMode = rng() % 3;
        const int offset = rng() % 30;
        const int oldOffsetAttack = (backfire != 2) * attack << (volatileMode == 1 ? 1 : 0);
        assert(Rules::qp_offset_attack(
            attack,
            Compat::modifier_mode_from_legacy(backfire),
            Compat::modifier_mode_from_legacy(volatileMode)) == oldOffsetAttack);
        int oldSend = offset;
        if (backfire == 2) oldSend = attack;
        else if (volatileMode == 1) oldSend >>= 1;
        assert(Rules::qp_outgoing_after_offset(
            offset, attack,
            Compat::modifier_mode_from_legacy(backfire),
            Compat::modifier_mode_from_legacy(volatileMode)) == oldSend);
        const int oldBackfire = backfire && oldSend >= 2 ? oldSend >> 1 : 0;
        assert(Rules::qp_backfire_self_garbage(
            Compat::modifier_mode_from_legacy(backfire), oldSend) == oldBackfire);

        const int previous = rng() % 8192;
        for (int mode : {1, 2}) {
            const std::optional<Rules::QPClearOperation> previousTyped =
                previous == 8191 ? std::nullopt
                                 : std::optional<Rules::QPClearOperation>(decode_qp_legacy(previous));
            const auto actual = Rules::evaluate_all_spin_repeat(
                Compat::all_spin_mode_from_legacy(mode), previousTyped, op);
            const auto expected = legacy_repeat(mode, previous, op);
            const int actualLegacy = actual.operation
                ? encode_qp_legacy(*actual.operation) : 8191;
            const int expectedLegacy = expected.operation
                ? encode_qp_legacy(*expected.operation) : 8191;
            assert(actualLegacy == expectedLegacy);
            assert(actual.solidGarbage == expected.solidGarbage);
            assert(actual.decayTimedSolidGarbage == expected.decayTimedSolidGarbage);
        }

        const int oldExt = Compat::to_legacy(op.pieceType) | (op.linesCleared << 7) |
                           (static_cast<int>(op.spin) << 10) |
                           (static_cast<int>(op.mini) << 11);
        assert(encode_extended_legacy(op) == oldExt);
        const int normalized = ((oldExt >> 10 & 1) ? oldExt : (oldExt | 127));
        assert(encode_extended_signature_legacy(
            Rules::extended_all_spin_signature(op)) == normalized);
    }
}

void test_history_equivalence() {
    std::mt19937 rng(31415926);
    std::vector<Runtime::ExtendedAllSpinSignature> actual;
    std::vector<int> expected;
    for (int i = 0; i < 10000; ++i) {
        const int op = rng() % 4096;
        double expectedMultiplier = 1.0;
        legacy_history(expected, op, expectedMultiplier);
        const double actualMultiplier = Rules::update_extended_all_spin_history(
            actual, decode_extended_signature_legacy(op));
        assert(actual.size() == expected.size());
        for (std::size_t j = 0; j < actual.size(); ++j) assert(encode_extended_signature_legacy(actual[j]) == expected[j]);
        assert(std::abs(actualMultiplier - expectedMultiplier) < 1e-12);
    }
}

using Field = std::vector<std::vector<int>>;

std::vector<Rules::RowMove> legacy_plan(int firstRow, int endRow, const std::vector<int>& clearedRows) {
    std::set<int> cleared(clearedRows.begin(), clearedRows.end());
    std::vector<Rules::RowMove> moves;
    int now = endRow;
    bool flag = false;
    for (int row = endRow - 1; row >= firstRow; --row) {
        --now;
        while (now >= firstRow + 1 && cleared.count(now)) --now, flag = true;
        now = std::max(now, firstRow + 1);
        if (!flag) continue;
        moves.push_back({row, now});
    }
    return moves;
}

void legacy_collapse(Field& field, int firstRow, int endRow, const std::vector<int>& clearedRows) {
    for (const auto& move : legacy_plan(firstRow, endRow, clearedRows)) {
        field[move.destination - firstRow] = field[move.source - firstRow];
    }
}

void test_collapse_equivalence() {
    std::mt19937 rng(27182818);
    constexpr int firstRow = -8;
    constexpr int endRow = 20;
    constexpr int width = 10;
    for (int iteration = 0; iteration < 20000; ++iteration) {
        std::vector<int> cleared;
        for (int row = firstRow; row < endRow; ++row) if (rng() % 7 == 0) cleared.push_back(row);

        const auto actualPlan = Rules::build_gravity_collapse_plan(firstRow, endRow, cleared);
        const auto expectedPlan = legacy_plan(firstRow, endRow, cleared);
        assert(actualPlan.size() == expectedPlan.size());
        for (std::size_t i = 0; i < actualPlan.size(); ++i) {
            assert(actualPlan[i].destination == expectedPlan[i].destination);
            assert(actualPlan[i].source == expectedPlan[i].source);
        }

        Field actual(endRow - firstRow, std::vector<int>(width));
        for (auto& row : actual) for (int& cell : row) cell = static_cast<int>(rng() % 12) - 1;
        Field expected = actual;
        legacy_collapse(expected, firstRow, endRow, cleared);
        Engine::apply_gravity_collapse(
            width,
            actualPlan,
            [&](int row, int col){ return Compat::board_cell_from_legacy(actual[row - firstRow][col]); },
            [&](int dst, int src){ actual[dst - firstRow] = actual[src - firstRow]; });
        assert(actual == expected);
    }
}


void test_anti_gravity_clear() {
    constexpr int firstRow = -5;
    constexpr int endRow = 6;
    constexpr int width = 4;
    Field field(endRow - firstRow, std::vector<int>(width, 3));
    const std::vector<int> cleared{-4, -2, 1, 5};
    const int minimumMutableRow = -2;
    const auto changes = Engine::apply_anti_gravity_clear(
        width,
        minimumMutableRow,
        cleared,
        [&](int row, int col){ return Compat::board_cell_from_legacy(field[row - firstRow][col]); },
        [&](int row, int col, Runtime::BoardCell value){ field[row - firstRow][col] = Compat::to_legacy(value); });

    for (int row = firstRow; row < endRow; ++row) {
        const bool shouldClear = row >= minimumMutableRow &&
            std::find(cleared.begin(), cleared.end(), row) != cleared.end();
        for (int col = 0; col < width; ++col) {
            assert(field[row - firstRow][col] == (shouldClear ? -1 : 3));
        }
    }
    assert(changes.size() == 3u * width);
    // Legacy order is bottom-to-top.
    assert(changes.front().row == 5);
    assert(changes.back().row == -2);
}

void test_timed_solid_decay_equivalence() {
    std::mt19937 rng(16180339);
    constexpr int firstRow = -4;
    constexpr int endRow = 10;
    constexpr int width = 6;
    for (int iteration = 0; iteration < 10000; ++iteration) {
        Field actual(endRow - firstRow, std::vector<int>(width));
        for (auto& row : actual) {
            for (int& cell : row) {
                const int r = rng() % 12;
                if (r < 3) cell = -1;
                else if (r < 7) cell = 7;
                else cell = 100 + (rng() % 5);
            }
        }
        Field expected = actual;
        int released = 0;
        for (int row = firstRow; row < endRow; ++row) {
            bool flag = false;
            for (int col = 0; col < width; ++col) {
                int& value = expected[row - firstRow][col];
                if (value > 100) --value;
                if (value == 100) flag = true;
            }
            if (flag) {
                for (int col = 0; col < width; ++col) {
                    int& value = expected[row - firstRow][col];
                    value = value == 100 ? -1 : 7;
                }
                ++released;
            }
        }
        const auto result = Engine::decay_timed_solid_garbage(
            width, firstRow, endRow,
            [&](int row, int col){ return Compat::board_cell_from_legacy(actual[row - firstRow][col]); },
            [&](int row, int col, Runtime::BoardCell value){ actual[row - firstRow][col] = Compat::to_legacy(value); });
        assert(actual == expected);
        assert(result.releasedRows == released);
    }
}

} // namespace

int run(int argc,char** argv) {
    test_qp_equivalence();
    test_history_equivalence();
    test_collapse_equivalence();
    test_anti_gravity_clear();
    test_timed_solid_decay_equivalence();
    return 0;
}
} // namespace TetrTests::post_lock_rules_test
