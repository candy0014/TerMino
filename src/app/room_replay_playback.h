#ifndef TETR_APP_ROOM_REPLAY_PLAYBACK_H
#define TETR_APP_ROOM_REPLAY_PLAYBACK_H
#include "tetr/replay/multiplayer_codec.h"
#include <memory>
namespace Runtime {class GameContext;}
namespace App {
// One scheduler applies recorded semantic observations through the same remote
// board adapter as live spectating. It never simulates a second game or sends
// attacks. Original operation streams remain in the archive for compatibility.
class RoomReplayPlayback {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    RoomReplayPlayback(Runtime::GameContext&,const Replay::MultiplayerReplay&);
    ~RoomReplayPlayback();
    bool valid() const;
    bool advance(std::uint64_t time);
    bool seek(std::uint64_t time);
};
}
#endif
