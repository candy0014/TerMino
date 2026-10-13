#ifndef TETR_REPLAY_TETRIO_RECORDER_H
#define TETR_REPLAY_TETRIO_RECORDER_H
#include "tetr/replay/tetrio_inputs.h"
#include "tetr/runtime/strong_types.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace Runtime { class GameContext; }
namespace Rules { struct PlacementResult; }
namespace Replay {
// Owned by one side of a local match; written only by that side's game thread.
class TetrioRecorder {
public:
    TetrioRecorder(std::uint32_t seed, int side);
    std::uint32_t seed() const { return seed_; }
    int received() const { return received_; }
    void hold();
    void begin_script(std::size_t length);
    void movement(char command, int rows, int columns);
    void incoming(const Runtime::GameContext& context, int lines);
    void cancellation(int placedPieces, bool doubled);
    void lock(const Runtime::GameContext& context, const Rules::PlacementResult& placement);
    std::string round_json(const Runtime::GameContext& context, bool won, std::int64_t endFrame);
    std::string audit_json() const;
    std::int64_t last_frame() const;
    static std::string user_json(int side);
    static std::string identity_json(int side);
private:
    int lock_phase() const { return side_ ? 14 : 7; }
    struct Event { std::int64_t frame; int priority; std::string json; };
    void flush_inputs();
    void ige(std::int64_t frame, const std::string& type, const std::string& data);
    std::string stats_json(const Runtime::GameContext& context, std::int64_t frames) const;
    std::uint32_t seed_;
    int side_, locks_ = 0, holds_ = 0, received_ = 0, igeId_ = 0, packetId_ = 0;
    int spins_ = 0;
    std::size_t inputCount_ = 0, scriptLength_ = 1, commandIndex_ = 0;
    bool doubled_ = false;
    int previousSent_ = 0, spike_ = 0, spikeNomult_ = 0, maxSpike_ = 0, maxSpikeNomult_ = 0;
    std::int64_t lastAttackFrame_ = -1000;
    std::int64_t pieceStartFrame_ = 0;
    TetrioInputEncoder inputs_;
    std::vector<Event> events_;
    std::vector<std::string> snapshots_;
    std::map<std::string, int> clears_;
};
TetrioRecorder* tetrio_recorder(Runtime::GameContext& context);
} // namespace Replay
#endif
