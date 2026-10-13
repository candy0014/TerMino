#ifndef TETR_ENGINE_GARBAGE_SERVICE_H
#define TETR_ENGINE_GARBAGE_SERVICE_H

#include "tetr/runtime/board_cell.h"
#include "tetr/runtime/garbage_hole_pattern.h"
#include "tetr/runtime/playfield.h"
#include <functional>
#include <optional>

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Engine::GarbageService {

void initialize(Runtime::PlayerSessionState& player, Runtime::GarbageHolePattern initialHolePattern);
void add_garbage(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::Playfield& field,
    int height,
    bool preserveHole = false,
    Runtime::BoardCell cell = Runtime::BoardCell::garbage(),
    bool renderField = true,
    std::optional<double> qpElapsedSeconds = std::nullopt);
void add_solid_garbage(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::Playfield& field,
    int height,
    Runtime::BoardCell cell = Runtime::BoardCell::solid_garbage());
void update_buffer(Runtime::GameContext& context, Runtime::PlayerSessionState& player, bool finalState = false);
void redraw_buffer(Runtime::GameContext& context, Runtime::PlayerSessionState& player);
bool has_ready_buffer(Runtime::GameContext& context, const Runtime::PlayerSessionState& player);
bool has_due_garbage_rise(Runtime::GameContext& context, const Runtime::PlayerSessionState& player);
// Called after each actual shift, so an active piece can be lifted one row at
// a time even when several deadlines are caught up in one displayed frame.
using GarbageRiseCallback=std::function<bool(int)>;
int advance_garbage_rise(Runtime::GameContext& context, Runtime::PlayerSessionState& player,
                         Runtime::Playfield& field, bool renderField=true,
                         const GarbageRiseCallback& afterRise={});
int raise_ready_buffer(Runtime::GameContext& context, Runtime::PlayerSessionState& player,
                      Runtime::Playfield& field, bool renderField = true, bool recordConsumption = true,
                      const GarbageRiseCallback& afterRise={});
void consume_recorded_buffer(Runtime::GameContext& context, Runtime::PlayerSessionState& player, int lines);
int resolve_received_attack(Runtime::GameContext& context, Runtime::PlayerSessionState& player, double attack);
void add_qp_opponent_attack(Runtime::GameContext& context,Runtime::PlayerSessionState& player,
                            Runtime::Playfield& field,int attack);
void add_buffer(Runtime::GameContext& context, Runtime::PlayerSessionState& player, Runtime::Playfield& field,
                int attack, bool instant=false, int resolvedLines=-1, bool deferDisplay=false,
                std::optional<int> receivedAttack=std::nullopt);
int offset_buffer(Runtime::GameContext& context, Runtime::PlayerSessionState& player, Runtime::Playfield& field,
                  int attack, int clearedLines, int b2bReleaseCharge = 0, int perfectClearAttack = 0);

} // namespace Engine::GarbageService

#endif
