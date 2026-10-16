'use strict';
const assert = require('node:assert/strict');
const {capturePressure} = require('../src/bot/chatgpt/analysis/moch_pressure.cjs');
const packet = (id, lines, active = true) => ({id, amt: lines, type: 'garbage',
    active, status: 'spawn', queued: false, delay: 0, column: 8, pos: [8], size: 1});
const state = packets => ({setoptions: {garbageentry: 'instant', garbagecap: 8,
    garbagecapmax: 40, shielded: 0}, impendingdamage: packets, waitingframes: [],
    garbageareentries: [], subframe: 0});
let s = state([packet(1, 3), packet(2, 5, false)]);
s.waitingframes.push({type: 'incoming-attack-hit', target: 120, data: {gid: 2, column: 9}});
const before = JSON.stringify(s);
let result = capturePressure(s, 100);
assert.equal(JSON.stringify(s), before);
assert.equal(result.mapping.eligible, true);
assert.equal(result.mapping.incoming, 8);
assert.equal(result.mapping.readyIncoming, 3);
assert.deepEqual(result.mapping.packets, [{lines: 3, secondsUntilReady: 0},
    {lines: 5, secondsUntilReady: 1 / 3}]);
assert.equal(JSON.stringify(result).includes('column'), false);
assert.equal(JSON.stringify(result).includes('"pos"'), false);
assert.equal(capturePressure(state([]), 100).mapping.eligible, true);

s = state([packet(1, 3, false)]);
assert(capturePressure(s, 100).mapping.reasons.includes('unknown_or_ambiguous_activation_deadline'));
s.waitingframes = [{type: 'incoming-attack-hit', target: 100, data: {gid: 1}}];
assert(capturePressure(s, 100).mapping.reasons.includes('activation_timer_at_or_before_observation'));
s.waitingframes[0].target = 120;
s.impendingdamage.push(packet(2, 1));
result = capturePressure(s, 100);
assert(result.mapping.reasons.includes('current_ready_not_fifo_prefix'));
assert(result.mapping.reasons.includes('non_fifo_ready_schedule'));
assert.equal(result.mapping.packets, null);
s.impendingdamage[1].active = false;
s.waitingframes.push({type: 'incoming-attack-hit', target: 110, data: {gid: 2}});
assert(capturePressure(s, 100).mapping.reasons.includes('non_fifo_ready_schedule'));

for (const [change, expected] of [
    [{shielded: true}, 'shielded_packet'], [{hardened: true}, 'hardened_packet'],
    [{queued: true, status: 'sleeping'}, 'queue_dependent_deadline'],
    [{status: 'caution'}, 'unsupported_packet_status'],
    [{type: 'other'}, 'nonordinary_packet'], [{amt: -1}, 'invalid_packet_lines'],
]) {
    s = state([{...packet(1, 2), ...change}]);
    assert(capturePressure(s, 100).mapping.reasons.includes(expected));
}
s = state([packet(1, 2)]);
s.garbageareentries = [{column: 3}];
assert(capturePressure(s, 100).mapping.reasons.includes('committed_garbage_outside_packet_queue'));
s = state([]); s.setoptions.garbagecapincrease = 1;
assert(capturePressure(s, 100).mapping.reasons.includes('time_varying_garbage_cap'));
console.log('moch_pressure_test: passed');
