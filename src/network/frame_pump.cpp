#include "tetr/network/frame_pump.h"

namespace Network {

MultiplayerFrameSignal pump_multiplayer_frame(
    MultiplayerFrameStateRef state,
    const FramePumpCallbacks& callbacks) {
    while (true) {
        const std::optional<FramePacket> packet = callbacks.receive ? callbacks.receive() : std::nullopt;
        if (!packet) return MultiplayerFrameSignal::Continue;

        const AppliedFramePacket applied = apply_frame_packet(*packet, state);
        if (applied.incomingAttack && callbacks.addIncomingAttack) {
            callbacks.addIncomingAttack(applied.incomingAttack);
        }
        if (applied.opponentState && callbacks.applyOpponentState) {
            callbacks.applyOpponentState(*applied.opponentState);
        }
        if (applied.opponentMotion && callbacks.applyOpponentMotion) callbacks.applyOpponentMotion(*applied.opponentMotion);
        if (applied.signal != MultiplayerFrameSignal::Continue) {
            return applied.signal;
        }
    }
}

} // namespace Network
