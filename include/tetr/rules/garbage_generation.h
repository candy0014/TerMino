#ifndef TETR_RULES_GARBAGE_GENERATION_H
#define TETR_RULES_GARBAGE_GENERATION_H

#include <random>
#include <vector>

#include "tetr/runtime/board_cell.h"
#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/garbage_hole_pattern.h"

namespace Rules {

struct GarbageGenerationConfig {
    int width = 10;
    Runtime::GarbageMode garbageMode = Runtime::GarbageMode::None;
    int cheeseModel = 1;
    double cheeseMessiness = 0.0;
    double singleMessiness = 0.0;
    Runtime::ModifierMode doubleHoleMode = Runtime::ModifierMode::Disabled;
    Runtime::BoardCell cell = Runtime::BoardCell::garbage();
    bool qpPolicy = false;
    double continuousMessiness = 0.0;
    double favor = 0.0;
    bool concentrate = false;
    std::vector<double> digScores;
    bool avoidSameColumn = false;
    bool legacyShapeRules = false;
    bool reverseNoHold = false;
};

struct GarbageGenerationState {
    std::vector<int> holeOrder;
    Runtime::GarbageHolePattern holePattern{};
    bool chessboardParity = false;
    int lastColumn = -1;
};

void prepare_garbage_batch(
    const GarbageGenerationConfig& config,
    GarbageGenerationState& state,
    std::mt19937& rng,
    bool preserveHole);

std::vector<Runtime::BoardCell> generate_garbage_row(
    const GarbageGenerationConfig& config,
    GarbageGenerationState& state,
    std::mt19937& rng);

std::vector<Runtime::BoardCell> generate_solid_garbage_row(
    int width,
    Runtime::BoardCell cell,
    Runtime::ModifierMode doubleHoleMode,
    int timedCountdown,
    GarbageGenerationState& state,
    std::mt19937& rng);

// Extend an existing row without moving its holes or changing placed cells.
// Empty/player-built rows gain empty space; garbage keeps its generation rule.
std::vector<Runtime::BoardCell> expand_garbage_row(
    const GarbageGenerationConfig& config,
    const std::vector<Runtime::BoardCell>& oldRow,
    int columnOffset,
    std::mt19937& rng);

} // namespace Rules

#endif
