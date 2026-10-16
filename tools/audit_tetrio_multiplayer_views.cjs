// Run the reviewed official client's original scope and tiny-board methods
// in an inert VM. No account, live match, or proprietary bundle is included.
'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const vm = require('node:vm');
const { reviewedSha256 } = require('./tetrio_engine_reference.cjs');

function audit(filename) {
    const bytes = fs.readFileSync(filename);
    const sha256 = crypto.createHash('sha256').update(bytes).digest('hex');
    if (sha256 !== reviewedSha256) throw Error('Unreviewed official client SHA-256: ' + sha256);
    const source = bytes.toString('utf8');
    const markers = {
        full_and_small_scope: 'case"full":case"small":e.iom.SetScoped(!0)',
        tiny_unscopes: 'default:e.iom.SetScoped(!1)',
        many_tiny_threshold: '(t>=9||ot.video.alwaystiny||"minimal"===ot.video.graphics)&&(r="tiny")',
        grid_tiny_threshold: '(this.activeGames.size>=9||ot.video.alwaystiny||"minimal"===ot.video.graphics)&&(a="tiny")',
        tiny_batch: 'lr.on("game.replay.board",(({boards:e})=>{if(cr&&pr)for(const{gameid:t,board:n}of e)cr.OnReplayBoardState(t,n)}))',
        crop_empty_rows: 'b:Vt(t.board.slice(e.bm.HighestLine()))',
        full_ignores_tiny: 'InjectBoardState:function(n){if(e.iom.IsSynced())return;',
        full_state_seek: 'n.siom.InjectState(e),n.esm.Seek(e.frame)',
    };
    const evidence = Object.fromEntries(Object.entries(markers).map(([name, marker]) => {
        const at = source.indexOf(marker);assert(at >= 0, 'Missing reviewed marker: ' + name);
        return [name, { utf8_byte_offset: Buffer.byteLength(source.slice(0, at)), marker }];
    }));
    // These two reviewed method bodies contain no string/template braces.
    function method(marker) {
        const start = source.indexOf(marker);assert(start >= 0);
        const fn = source.indexOf('function', start), open = source.indexOf('{', fn);
        let depth = 1, end = open + 1;
        for (; end < source.length && depth; ++end) {
            if (source[end] === '{') ++depth;
            else if (source[end] === '}') --depth;
        }
        assert.equal(depth, 0);return source.slice(fn, end);
    }
    let now = 0, serial = 0, synced = false;
    const timers = new Map(), sent = [];
    const self = {
        W: 10, H: 20, T: 40,
        gsm: { board: Array.from({ length: 40 }, (_, row) => Array(10).fill(row < 38 ? null : 'i')),
            fire: 7, garbagereceived: 9, setoptions: { boardwidth: 10, boardheight: 20 } },
        bm: { HighestLine: () => 38 }, iom: { IsSynced: () => synced },
        hm: { H: {} }, lm: { H: {} },
    };
    const sandbox = vm.createContext({ self, k: class {},
        Vt: value => JSON.parse(JSON.stringify(value)),
        setTimeout: (fn, delay) => { const id = ++serial;timers.set(id, { fn, at: now + delay });return id; },
        clearTimeout: id => timers.delete(id),
    });
    const start = source.indexOf('class j extends k{'), end = source.indexOf('const K=function(e)', start);
    assert(start >= 0 && end > start);
    vm.runInContext(source.slice(start, end) + ';globalThis.IO=j;' +
        'const e=self,t=e.gsm;globalThis.board={' +
        'eject:' + method('EjectBoardState:function()') + ',' +
        'inject:' + method('InjectBoardState:function(') + '};', sandbox, { timeout: 1000 });
    const tiny = sandbox.board.eject();
    assert.deepEqual(Object.keys(tiny).sort(), ['b', 'f', 'g', 'h', 'w']);
    assert.equal(tiny.b.length, 2);assert.equal(tiny.f, 7);assert.equal(tiny.g, 9);
    tiny.b[0][0] = 't';assert.equal(self.gsm.board[38][0], 'i');
    sandbox.board.inject(tiny);
    assert.equal(self.gsm.board.length, 40);assert.equal(self.gsm.board[0][0], null);
    assert.equal(self.gsm.board[38][0], 't');assert(self.lm.H.mustRerenderTiny);
    synced = true;const before = self.gsm.board;
    sandbox.board.inject({ ...tiny, b: [] });assert.equal(self.gsm.board, before);

    let drained = 0;
    const io = Object.assign(Object.create(sandbox.IO.prototype), {
        id: 17, self: { esm: { IsKeyboard: () => false, IsSocket: () => true },
            wfm: { ExecuteUnsafeWaitingFrames: () => ++drained } },
        _scoped: true, _synced: true, _timeoutID: null,
        _socket: { send: (name, data) => sent.push({ at_ms: now, name, data }) },
    });
    const advance = ms => { now += ms;for (const [id, timer] of timers) if (timer.at <= now) { timers.delete(id);timer.fn(); } };
    io.SetScoped(false);assert.equal(sent[0].name, 'game.scope.end');assert.equal(sent[0].at_ms, 0);
    assert.equal(io.IsSynced(), false);assert.equal(drained, 1);
    io.SetScoped(true);advance(299);assert.equal(sent.length, 1);
    advance(1);assert.equal(sent[1].name, 'game.scope.start');assert.equal(sent[1].at_ms, 300);
    assert.equal(io.IsSynced(), false); // A request alone is not a full-state anchor.
    io.synced = true;assert.equal(io.IsSynced(), true);
    io.SetScoped(false);io.SetScoped(true);advance(100);io.SetScoped(false);advance(300);
    assert.equal(sent.filter(event => event.name === 'game.scope.start').length, 1);
    return { source_url: 'https://tetr.io/js/tetrio.js', sha256, source_bytes: bytes.length,
        method: 'Pinned client static audit and isolated original-method execution; no live match capture.',
        evidence, tiny_keys: Object.keys(tiny), cropped_rows: tiny.b.length, storage_rows: self.T,
        tiny_copy_is_independent: true, tiny_restores_empty_top_rows: true, synced_full_ignores_tiny: true,
        scope_end_is_immediate: true, scope_start_debounce_ms: 300,
        scope_start_needs_state_anchor: true, canceled_scope_start_is_not_sent: true, scope_events: sent };
}
if (require.main === module) {
    const [, , filename, output] = process.argv;
    if (!filename) throw Error('Usage: node audit_tetrio_multiplayer_views.cjs OFFICIAL_CLIENT.js [REPORT.json]');
    const result = audit(filename), json = JSON.stringify(result, null, 2) + '\n';
    if (output) fs.writeFileSync(output, json);else process.stdout.write(json);
}
module.exports = { audit };
