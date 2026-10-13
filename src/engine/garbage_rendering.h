#ifndef TETR_ENGINE_GARBAGE_RENDERING_H
#define TETR_ENGINE_GARBAGE_RENDERING_H

#include "tetr/engine/garbage_field.h"
#include "tetr/runtime/piece.h"

#include <optional>
#include <vector>

namespace Runtime {
class Playfield;
class GameContext;
struct PlayerSessionState;
}

namespace Engine::GarbageRendering {

struct FieldImage {
    struct Cell {
        int color = -1;
        int number = 0;
        bool filled = false;
        bool dimmed = false;
        bool operator==(const Cell& other) const {
            return filled==other.filled && number==other.number &&
                ((!filled && !number) || (color==other.color && dimmed==other.dimmed));
        }
    };
    int width = 0;
    int bottom = 0;
    std::vector<Cell> cells;
};

// Capture only the visible field, optionally including its active piece and ghost.
FieldImage capture_field(
    const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,
    const Piece::Definition* piece = nullptr,
    std::optional<Piece::Pose> pose = {});

// Emit changed cells in contiguous runs; the caller chooses when to flush.
void render_field_changes(
    const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,
    const FieldImage& before,
    const Piece::Definition* piece = nullptr,
    std::optional<Piece::Pose> pose = {});

} // namespace Engine::GarbageRendering

#endif
