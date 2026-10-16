// Offline research only. Supply the reviewed official client yourself.
// No login, live game connection, or proprietary source is saved in the repo.
'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const vm = require('node:vm');
const { reviewedSha256 } = require('./tetrio_engine_reference.cjs');

function audit(filename) {
    const bytes = fs.readFileSync(filename);
    const digest = crypto.createHash('sha256').update(bytes).digest('hex');
    if (digest !== reviewedSha256) throw Error('Unreviewed official client SHA-256: ' + digest);
    const source = bytes.toString('utf8');
    const section = (start, end) => {
        const begin = source.indexOf(start);
        const finish = source.indexOf(end, begin + start.length);
        if (begin < 0 || finish <= begin) throw Error('Missing reviewed section: ' + start);
        return source.slice(begin, finish);
    };
    const markers = {
        ribbon_websocket: 'new WebSocket(this._uri,this._spoolid)',
        ribbon_direct_send: '_send_internal(e){try{this._ws.send(e)',
        ribbon_resume_cache: 'this._sentQueue.map((e=>e.packet))',
        endpoint_discovery: 'xn.get("/api/server/ribbon")',
        route_reassessment: 'E("AmbientRespool",C)',
        replay_wire: 'this._rollingTarget.send("game.replay",new Me(t,n,s))',
        local_keyboard: '_AttachLocal(e){const t=new v("keyboard",{socket:this._socket})',
        remote_socket: '_AttachOnline(e,t){const n=new v("socket",{socket:this._socket})',
        remote_provision_gate: 'IsNextFrameReady(){return!this.IsSocketLike()||this._provision>this._frame}',
        remote_catchup: 'IsFallingBehind(){return!!this.IsSocketLike()&&this._provision>this._frame+60}',
        scoped_full_updates: 'this.SendToServer("game.scope.start",this.id)',
        scoped_stop: 'this.SendToServer("game.scope.end",this.id)',
        mini_board_updates: 'lr.on("game.replay.board",',
        state_correction: 'lr.on("game.replay.state",',
        attack_delivery: 'this.socket.on("game.replay.ige",e)',
        attack_acknowledgement: 'if(o.iid<=e.ackiid)continue;',
        interaction_deferral_types: 'this._defIGEs=new Set(["interaction_confirm","kev","targeted"])',
        interaction_frame_alignment: 't.awaitFrame(e.data.frame)',
        binary_replay_encoding: 'e.writeUInt(this.gameid,13),e.writeDInt(n,t.$prov),e.pack(this.frames)',
        critical_flush: 's.includes(i)&&e.iom.replay.FlushNextFrame()',
    };
    const evidence = Object.fromEntries(Object.entries(markers).map(([name, marker]) => {
        const at = source.indexOf(marker);
        assert(at >= 0, 'Missing reviewed marker: ' + name);
        return [name, { utf8_byte_offset: Buffer.byteLength(source.slice(0, at)), marker }];
    }));

    // Evaluate the original, unchanged Replay class. A callback replaces only
    // its external transport; qt=false selects its client scheduling branch.
    const replaySource = section('class L{static SLOWEST_PROVISION_SPEED=', 'class B{');
    const replayContext = vm.createContext({
        Vt: value => JSON.parse(JSON.stringify(value)), qt: () => false,
    });
    vm.runInContext(replaySource + ';globalThis.Replay=L;', replayContext, { timeout: 1000 });
    const Replay = replayContext.Replay;
    const scheduling = [];
    for (const mode of ['zero', 'low', 'medium', 'high', 'xhigh']) {
        for (const scenario of ['idle', 'keydown_every_6_frames', 'forced_flush_every_6_frames']) {
            const replay = new Replay({ options: { gameid: 1 } });
            const batches = [];
            replay.SetLatencyMode(mode);
            replay.BindRollingTarget(packet => batches.push({
                advance_frame_call: replay.frame + 1,
                provisioned_frame: packet.provisioned,
                event_frames: Array.from(packet.frames, event => event.frame),
            }));
            for (let frame = 0; frame < 120; ++frame) {
                if (scenario !== 'idle' && frame % 6 === 0) {
                    replay.PushEvent('keydown', { key: 'moveLeft', subframe: 0 });
                    if (scenario === 'forced_flush_every_6_frames') replay.FlushNextFrame();
                }
                replay.AdvanceFrame();
            }
            if (scenario === 'forced_flush_every_6_frames') {
                assert.equal(batches.length, 20);
                batches.forEach((packet, index) => assert.equal(packet.advance_frame_call, index * 6 + 1));
            } else if (scenario === 'keydown_every_6_frames') {
                assert.equal(batches[0].advance_frame_call, ['zero', 'low'].includes(mode) ? 15 : 50);
            }
            scheduling.push({ mode, scenario, simulated_frames: 120, batches });
        }
    }

    // Use the original event-source methods without constructing DOM listeners.
    // The empty base class is unused: no constructor or inherited method runs.
    const eventSource = section('class P extends k{', 'const W=function(e)');
    const eventContext = vm.createContext({ k: class {} });
    vm.runInContext(eventSource + ';globalThis.EventSource=P;', eventContext, { timeout: 1000 });
    const makeSource = type => Object.assign(Object.create(eventContext.EventSource.prototype), {
        _type: type, _gameid: 7, _frame: 0, _events: [], _provision: 0, _lastProvision: 15,
    });
    const local = makeSource('keyboard'), remote = makeSource('socket');
    assert.equal(local.IsNextFrameReady(), true);
    assert.equal(remote.IsNextFrameReady(), false);
    remote._OnReplayData({ gameid: 8, provisioned: 10, frames: [] });
    assert.equal(remote.IsNextFrameReady(), false);
    remote._OnReplayData({ gameid: 7, provisioned: 10, frames: [] });
    let advanced = 0;
    while (remote.IsNextFrameReady()) { remote.AdvanceFrame(); ++advanced; }
    assert.equal(advanced, 10);
    assert.equal(remote.IsNextFrameReady(), false);
    remote._OnReplayData({ gameid: 7, provisioned: 80, frames: [] });
    assert.equal(remote.IsFallingBehind(), true);

    const batchConstantOccurrences = source.split('_BATCH_TIMEOUT').length - 1;
    assert.equal(batchConstantOccurrences, 1); // Declaration alone is not a send timer.
    return {
        source_url: 'https://tetr.io/js/tetrio.js', sha256: digest, source_bytes: bytes.length,
        method: 'Pinned client static audit and isolated original-method execution; no live match capture.',
        evidence, ribbon_batch_timeout_occurrences: batchConstantOccurrences,
        provision_lookup: JSON.parse(JSON.stringify(Replay.PROVISION_SPEED_LOOKUP)),
        scheduling,
        event_source_checks: {
            local_without_remote_data_ready: true, remote_without_data_ready: false,
            wrong_game_ignored: true, frames_allowed_by_provision_10: advanced,
            remote_stops_at_provision: true, behind_over_60_frames_detected: true,
        },
        limits: [
            'Scheduling cases are synthetic method calls, not real match packet timings.',
            'Forced flush is not asserted to occur on every keypress or hard drop.',
            'Server implementation, live room/Tetra League options, and real RTT were not captured.',
            'No physical input-to-display latency is measured.',
        ],
    };
}

if (require.main === module) {
    try {
        if (process.argv.length < 3 || process.argv.length > 4)
            throw Error('Usage: node tools/audit_tetrio_netcode.cjs OFFICIAL_CLIENT_JS [OUTPUT_JSON]');
        const result = audit(process.argv[2]);
        const json = JSON.stringify(result, null, 2) + '\n';
        if (process.argv[3]) {
            fs.writeFileSync(process.argv[3], json);
            console.log(JSON.stringify({ sha256: result.sha256, markers: Object.keys(result.evidence).length,
                scheduling_cases: result.scheduling.length, event_source_checks: result.event_source_checks,
                output: process.argv[3] }));
        } else process.stdout.write(json);
    } catch (error) {
        console.error(error.message);
        process.exitCode = 1;
    }
}
module.exports = { audit };
