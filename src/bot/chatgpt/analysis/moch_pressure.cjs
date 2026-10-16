'use strict';

// Offline adapter for the reviewed TETR.IO engine. Read only the state at this
// observation; never replay future events to infer an acknowledgement/deadline.
// Scope: already visible ordinary packets with a static, FIFO-compatible ready
// schedule. This does not claim equivalence of the two complete game rulesets.
const {reviewedSha256} = require('../../../../tools/tetrio_engine_reference.cjs');

function capturePressure(state, frame) {
    const reasons = [];
    const reject = reason => { if (!reasons.includes(reason)) reasons.push(reason); };
    const options = state.setoptions || {};
    if (!Number.isInteger(frame) || frame < 0) reject('invalid_frame');
    if (!Array.isArray(state.impendingdamage)) reject('missing_packet_state');
    if (!Array.isArray(state.waitingframes)) reject('missing_timer_state');
    if (options.garbageentry !== 'instant') reject('noninstant_garbage_entry');
    if (Math.floor(Math.min(options.garbagecap, options.garbagecapmax)) !== 8)
        reject('garbage_cap_not_eight');
    if (options.garbagecapincrease) reject('time_varying_garbage_cap');
    if (options.shielded > frame) reject('active_player_shield');
    if ((state.garbageareentries || []).length ||
        (state.waitingframes || []).some(timer => timer.type === 'push-garbage-line'))
        reject('committed_garbage_outside_packet_queue');

    // Explicit whitelists: column, size, pos/neg, position, actor positions,
    // acknowledgement IDs, RNG state and the opponent's future events never
    // leave this adapter. Packet IDs only join already scheduled local timers.
    const packets = (state.impendingdamage || []).map(packet => ({
        id: packet.id, lines: packet.amt, type: packet.type ?? null,
        active: packet.active === true, status: packet.status,
        hardened: !!packet.hardened, shielded: !!packet.shielded,
        queued: !!packet.queued, delayFrames: packet.delay ?? null,
        firstcycle: !!packet.firstcycle,
    }));
    const ids = new Set(packets.map(packet => packet.id));
    const timers = (state.waitingframes || []).flatMap(timer => {
        const id = timer.type === 'incoming-attack-hit' ? timer.data?.gid :
            timer.type === 'process-garbage-status' ? timer.data?.id : undefined;
        return ids.has(id) ? [{packetId: id, type: timer.type, targetFrame: timer.target}] : [];
    });
    if (ids.size !== packets.length) reject('duplicate_packet_id');
    let incoming = 0, readyVisible = 0;
    const visiblePackets = [];
    for (const packet of packets) {
        if (!Number.isInteger(packet.id)) reject('invalid_packet_id');
        if (!Number.isInteger(packet.lines) || packet.lines <= 0) reject('invalid_packet_lines');
        else incoming += packet.lines;
        if (packet.shielded) reject('shielded_packet');
        if (packet.hardened) reject('hardened_packet');
        if (packet.queued) reject('queue_dependent_deadline');
        if (packet.type !== 'garbage') reject('nonordinary_packet');
        const readyNow = !packet.shielded && packet.active && packet.status === 'spawn';
        if (readyNow) readyVisible += packet.lines;
        let deadline = null;
        if (packet.status !== 'spawn') {
            // Caution/danger have multiple transitions; sleeping may depend on
            // the chosen cancellation path. Leave them explicit, not guessed.
            reject('unsupported_packet_status');
        } else if (packet.active) {
            deadline = frame;
        } else {
            const hits = timers.filter(timer => timer.packetId === packet.id &&
                timer.type === 'incoming-attack-hit');
            if (hits.length !== 1) reject('unknown_or_ambiguous_activation_deadline');
            else if (!Number.isInteger(hits[0].targetFrame) || hits[0].targetFrame <= frame)
                reject('activation_timer_at_or_before_observation');
            else deadline = hits[0].targetFrame;
        }
        visiblePackets.push({lines: packet.lines,
            secondsUntilReady: deadline === null ? null : (deadline - frame) / 60});
    }
    let readyIncoming = 0, blocked = false, previousDeadline = -Infinity;
    for (const packet of visiblePackets) {
        const deadline = packet.secondsUntilReady;
        if (deadline === null) { blocked = true; continue; }
        // Official TakeAllDamage skips unready entries, whereas our host eats a
        // ready prefix. Monotone deadlines ensure those schedules agree after
        // any prefix cancellation/rise; never sort to manufacture agreement.
        if (deadline < previousDeadline) reject('non_fifo_ready_schedule');
        previousDeadline = deadline;
        if (deadline > 0) blocked = true;
        if (!blocked) readyIncoming += packet.lines;
    }
    if (readyIncoming !== readyVisible) reject('current_ready_not_fifo_prefix');
    return {
        schema_version: 1, reference_sha256: reviewedSha256,
        frame, subframe: Number.isFinite(state.subframe) ? state.subframe : null,
        clock: 'engine frame; 60 frames per second; no future event subframe',
        packets, timers, pendingRaw: incoming, readyVisible,
        mapping: {
            eligible: reasons.length === 0, reasons, incoming, readyIncoming,
            // Null means callers MUST NOT construct a native root from this.
            packets: reasons.length ? null : visiblePackets,
            forecastStepSeconds: 1,
        },
    };
}

module.exports = {capturePressure};
