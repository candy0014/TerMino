#ifndef TETR_NETWORK_FRAME_PUMP_H
#define TETR_NETWORK_FRAME_PUMP_H

#include "tetr/network/frame_packet.h"

#include <functional>
#include <optional>

namespace Network {

struct FramePumpCallbacks {
    std::function<std::optional<FramePacket>()> receive;
    std::function<void(int)> addIncomingAttack;
    std::function<void(const BoardStateSnapshot&)> applyOpponentState;
    std::function<void(const PieceMotionFrame&)> applyOpponentMotion;
};

MultiplayerFrameSignal pump_multiplayer_frame(
    MultiplayerFrameStateRef state,
    const FramePumpCallbacks& callbacks);

} // namespace Network

#endif
