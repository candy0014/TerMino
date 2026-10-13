#ifndef TETR_REPLAY_TETRIO_INPUTS_H
#define TETR_REPLAY_TETRIO_INPUTS_H

#include <cstdint>
#include <string>
#include <vector>

namespace Replay {

// Serialize the host's actual catalog, not a presumed stock SRS+ table. The
// client accepts this through a custom/constants IGE at the start of a round.
std::string tetrio_native_rotation_profile();

struct TetrioKeyEvent {
    std::int64_t frame = 0;
    double subframe = 0;
    std::string key;
    bool down = false;
};

// TETR.IO engine v19, zero gravity, SDF=20, may20g=false, safelock=false.
// A positive-duration softDrop pulse advances exactly one cell in that profile.
// A same-subframe keydown/keyup does NOT advance a cell. Never use SDF=41 for S.
// This encoder consumes executed commands and their measured displacement; it
// does not choose a movement path or infer a path from a final landing position.
class TetrioInputEncoder {
public:
    static constexpr int softDropFactor = 20;
    static constexpr int ticksPerFrame = 10000;

    // A command reserves its own subframe interval. Colliding timestamps are
    // serialized; a soft-drop pulse never straddles a frame boundary.
    void advance_to(std::int64_t frame, double subframe = 0);
    void hold();
    void movement(char command, int rowsMoved, int columnsMoved);
    const std::vector<TetrioKeyEvent>& events() const { return events_; }

private:
    void tap(const char* key, bool softDrop = false);
    std::int64_t tick_ = 0;
    std::vector<TetrioKeyEvent> events_;
};

} // namespace Replay

#endif
