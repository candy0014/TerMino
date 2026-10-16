// Offline timer research against a pinned official client. No live connections.
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
        const begin = source.indexOf(start), finish = source.indexOf(end, begin + start.length);
        if (begin < 0 || finish <= begin) throw Error('Missing reviewed section: ' + start);
        return source.slice(begin, finish);
    };
    const markers = {
        displayed_time: 'n=e.zero?0:(this.self.esm.frame-e.frameoffset)/60*1e3',
        per_board_counter: 't.setoptions[`slot_counter${i}`]===n&&e.hm.H.board.el(`counter${i}`).update(s)',
        wall_clock_start: 'StartStopwatch(){this.S.time.start=performance.now(),this.S.time.zero=!1}',
        low_resolution: 'ot.video.lowrescounters||"minimal"===ot.video.graphics?`${i.m}:${i.s}`',
        tiny_counter_guard: 'e.hm.IsTinyMode()||(e.dsm.UpdateCounts(),e.dsm.UpdateTimers())',
        game_time_api: 'GetGameTime:function(){return 1e3*(e.esm.frame+t.subframe)/60}',
        remote_gate: 'IsNextFrameReady(){return!this.IsSocketLike()||this._provision>this._frame}',
        large_backlog: 'IsFallingBehind(){return!!this.IsSocketLike()&&this._provision>this._frame+60}',
        mild_catchup: 'if(e.esm.GetBehindness()>=1.25)',
        local_multiplayer_start: 'for(const e of this._games.values())t.push(e[e===this._games.self?"startGame":n]())',
    };
    const evidence = Object.fromEntries(Object.entries(markers).map(([key, marker]) => {
        const at = source.indexOf(marker);
        assert(at >= 0, 'Missing reviewed marker: ' + key);
        return [key, { utf8_byte_offset: Buffer.byteLength(source.slice(0, at)), marker }];
    }));

    let clock = 10000;
    const video = { lowrescounters: false, graphics: 'high', siren: false };
    const sandbox = vm.createContext({
        performance: { now: () => clock }, ot: { video }, Ra: () => {},
        Vo: { addGameM: () => {} }, o: { Ticker: { shared: { remove: () => {} } } },
    });
    // Original classes, formatter, and loop factory, unchanged. The game rules
    // and renderer are inert fixture dependencies: these cases test timing and
    // event delivery gates, not actual gameplay, transport latency, or pixels.
    const loop = section('Y=function(e)', ',Q=function(e)').slice(2);
    const original = section('class k{constructor(e)', 'const w={') +
        section('const D=function(e)', 'class O extends k{') +
        section('class O extends k{', 'class P extends k{') +
        section('class P extends k{', 'const W=function(e)') +
        section('function wt(e){const t=Math.round(e)', 'function Lt(e)') +
        ';const makeLoop=' + loop + ';globalThis.ref={Counters:O,EventSource:P,counterRouter:D,makeLoop};';
    vm.runInContext(original, sandbox, { timeout: 1000 });
    const ref = sandbox.ref, noop = () => {};
    function fixture(type, frame, tiny = false) {
        const counters = new Map();
        const options = { gameid: 7, boardwidth: 10, boardheight: 20, boardbuffer: 20,
            slot_counter1: 'stopwatch', slot_counter2: 'timer', objective_type: 'none', noreplay: true };
        const self = {
            gsm: { setoptions: options, time: { zero: false, locked: false, frameoffset: 0 },
                subframe: 0, started: true },
            c: { IsServer: () => false },
            iom: { IsLocal: () => type === 'keyboard', IsSynced: () => true },
            elm: { emit: noop },
            hm: { IsTinyMode: () => tiny, IsFullMode: () => false, PullHolder: noop,
                H: { stackmud: [], board: { el: key => ({ update: value => counters.set(key, value) }), progress: noop } } },
            im: { ProcessAllShift: noop, ProcessInterrupts: noop }, fm: { Fall: noop },
            wfm: { ExecuteWaitingFrames: noop, ExecuteUnsafeWaitingFrames: noop },
            bm: { ProcessGarbageARE: noop }, rbm: { CheckObjectiveCleared: noop },
            spm: { LowerSpikeClock: noop }, atm: { ProcessHesitatedAttacks: noop },
            svm: { Update: noop }, tm: { Update: noop }, am: { ProgressActors: noop, PullActors: noop },
            trm: { Update: noop }, zthm: { Update: noop }, sfx: { Update: noop },
            sdm: { DrawFallingOpacity: noop }, stm: { H: { stockExploding: false } },
        };
        // Avoid DOM listener setup; all executed EventSource methods are original.
        self.esm = Object.assign(Object.create(ref.EventSource.prototype), {
            self, _type: type, _gameid: 7, _frame: frame, _events: [], _listeners: new Map(),
            _provision: frame, _lastProvision: 15, _lastFrameTime: clock,
            _frameLength: 1000 / 60, _frameDiffs: [],
        });
        self.ctm = ref.counterRouter(self);
        self.dsm = new ref.Counters(self);
        self.dsm.UpdateCounts = noop;
        self.lm = ref.makeLoop(self);
        return { self, counters, display() {
            self.dsm.UpdateTimers();
            return String(counters.get('counter1')).replace(/\f3/g, '');
        } };
    }

    const cases = [];
    const local = fixture('keyboard', 635), remote = fixture('socket', 629);
    assert.equal(local.display(), '0:10.583');
    assert.equal(remote.display(), '0:10.483');
    cases.push({ case: 'independent_frame_timers', local_frame: 635, remote_frame: 629,
        local_display: local.display(), remote_display: remote.display(), frame_gap_ms: 100 });
    video.lowrescounters = true;
    assert.equal(local.display(), '0:10'); assert.equal(remote.display(), '0:10');
    cases.push({ case: 'whole_seconds_hide_100ms_gap', local_display: local.display(), remote_display: remote.display() });
    video.lowrescounters = false;
    const beforeClock = remote.display(); clock += 5000;
    assert.equal(remote.display(), beforeClock);
    cases.push({ case: 'wall_clock_advance_without_simulation', wall_clock_advance_ms: 5000, display: remote.display() });

    for (let tick = 0; tick < 60; ++tick) { local.self.lm.Update(); remote.self.lm.Update(); }
    assert.equal(local.self.esm.frame, 695); assert.equal(remote.self.esm.frame, 629);
    assert.equal(local.display(), '0:11.583'); assert.equal(remote.display(), '0:10.483');
    cases.push({ case: 'no_delivery_for_60_requested_ticks', local_frame: local.self.esm.frame,
        remote_frame: remote.self.esm.frame, local_display: local.display(), remote_display: remote.display() });

    const large = fixture('socket', 0);
    large.self.esm._OnReplayData({ gameid: 7, provisioned: 120, frames: [] });
    large.self.lm.Update();
    assert.equal(large.self.esm.frame, 60);
    cases.push({ case: 'large_backlog_recursive_catchup', provisioned_frame: 120,
        requested_loop_updates: 1, advanced_frames: large.self.esm.frame, display: large.display() });

    const mild = fixture('socket', 0);
    mild.self.esm._OnReplayData({ gameid: 7, provisioned: 30, frames: [] });
    mild.self.esm._OnReplayData({ gameid: 7, provisioned: 40, frames: [] });
    for (let tick = 0; tick < 10; ++tick) mild.self.lm.Update();
    assert(mild.self.esm.frame > 10 && mild.self.esm.frame <= 40);
    cases.push({ case: 'mild_backlog_extra_frames', provisioned_frame: 40,
        requested_loop_updates: 10, advanced_frames: mild.self.esm.frame, display: mild.display() });

    const tiny = fixture('socket', 629, true), full = fixture('socket', 629);
    tiny.self.lm.Render(); full.self.lm.Render();
    assert.equal(tiny.counters.has('counter1'), false);
    assert.equal(full.counters.get('counter1').replace(/\f3/g, ''), '0:10.483');
    cases.push({ case: 'tiny_render_skips_timer_update', tiny_timer_updated: false,
        full_timer_updated: true, full_display: full.display() });

    remote.self.gsm.time.locked = true;
    const locked = remote.display(); remote.self.esm._frame += 60;
    assert.equal(remote.display(), locked);
    cases.push({ case: 'locked_timer_stays_fixed', display: locked });

    const offset = fixture('socket', 635); offset.self.gsm.time.frameoffset = 600;
    assert.equal(offset.display(), '0:00.583');
    offset.self.gsm.subframe = 0.5;
    assert.equal(offset.display(), '0:00.583');
    assert(Math.abs(offset.self.lm.GetGameTime() - (635.5 / 60 * 1000)) < 1e-9);
    cases.push({ case: 'display_offset_and_subframe', frame: 635, offset: 600, subframe: 0.5,
        display: offset.display(), game_time_api_ms: offset.self.lm.GetGameTime() });

    return {
        source_url: 'https://tetr.io/js/tetrio.js', sha256: digest, evidence, cases,
        method: 'Original timer, formatter, counter routing, event-source methods and loop; inert gameplay/render dependencies.',
        limits: ['Synthetic offline cases, not a live match or network latency measurement.',
            'No physical screen pixels, remote computer clock, or server synchronization accuracy was measured.'],
    };
}

if (require.main === module) {
    try {
        if (process.argv.length < 3 || process.argv.length > 4)
            throw Error('Usage: node tools/audit_tetrio_timers.cjs OFFICIAL_CLIENT_JS [OUTPUT_JSON]');
        const result = audit(process.argv[2]), json = JSON.stringify(result, null, 2) + '\n';
        if (process.argv[3]) {
            fs.writeFileSync(process.argv[3], json);
            console.log(JSON.stringify({ sha256: result.sha256, markers: Object.keys(result.evidence).length,
                cases: result.cases, output: process.argv[3] }));
        } else process.stdout.write(json);
    } catch (error) { console.error(error.message); process.exitCode = 1; }
}
module.exports = { audit };
