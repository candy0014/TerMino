'use strict';
const assert = require('node:assert/strict');
const { execFileSync } = require('node:child_process');
const { loadReference } = require('../tools/tetrio_engine_reference.cjs');

if (process.argv.length !== 4) {
    console.error('Usage: node tests/tetrio_inputs_reference_test.cjs <tetrio.js> <tetrio_inputs_test>');
    process.exit(2);
}
const engine = loadReference(process.argv[2]);
const fixtures = JSON.parse(execFileSync(process.argv[3], ['--fixtures'], { encoding: 'utf8' }));
function create(events, sdf = 20) {
    const game = engine.create('replay', {replay: new engine.Replay({frames:10, events})});
    game.setHeadless(true);
    game.setGame({version:19, seed:1, g:0, gincrease:0, allow180:true,
        spinbonuses:'all-mini+', nolockout:true,
        handling:{arr:0, das:4, dcd:0, sdf, safelock:false, cancel:false,
            may20g:false, irs:'off', ihs:'off'}});
    game.startGame();
    return game;
}
let checked = 0;
for (const name of ['single', 'boundary']) {
    const game = create(fixtures[name]);
    const before = Math.ceil(game.state.falling.y);
    game.doFrame();
    if (name === 'boundary') {
        assert.equal(Math.ceil(game.state.falling.y), before);
        game.doFrame();
    }
    assert.equal(Math.ceil(game.state.falling.y), before + 1, name);
    game.doFrame(); // no held key leaking into a later frame
    assert.equal(Math.ceil(game.state.falling.y), before + 1, name + ' key release');
    ++checked;
}
{
    const game = create(fixtures.floor);
    game.doFrame();
    assert.equal(Math.ceil(game.state.falling.y), 39); // O piece on empty floor
    assert.equal(game.state.stats.piecesplaced, 0); // soft drop must NOT lock
    ++checked;
}
{
    const game = create(fixtures.single_rotate);
    const before = Math.ceil(game.state.falling.y);
    game.doFrame();
    assert.equal(Math.ceil(game.state.falling.y), before + 1);
    assert.equal(game.state.falling.r, 1);
    ++checked;
}
{
    const game = create(fixtures.blocked);
    const state = game.ejectState();
    state.game.falling.y = 38.96;
    game.injectState(state);
    game.doFrame();
    assert.equal(Math.ceil(game.state.falling.y), 39);
    assert.equal(game.state.stats.piecesplaced, 0);
    ++checked;
}
// Negative controls prove the test catches both naive encodings of S.
{
    const game = create(fixtures.spin_double);
    const state = game.ejectState();
    Object.assign(state.game.falling, {type:'t', x:4, y:36.96, r:1, hy:37});
    state.game.board[37][3] = 'gb';
    for (let x = 0; x < 10; ++x) {
        state.game.board[38][x] = [3,4,5].includes(x) ? null : 'gb';
        state.game.board[39][x] = x === 4 ? null : 'gb';
    }
    game.injectState(state);
    game.doFrame();
    assert.equal(game.state.stats.piecesplaced, 1);
    assert.equal(game.state.stats.lines, 2);
    assert.equal(game.state.stats.clears.tspindoubles, 1);
    assert.equal(game.state.stats.clears.minitspindoubles, 0);
    ++checked;
}
{
    // Native I 3->0 and 180-degree kicks differ from stock SRS+. Preserve the
    // actual host rules with a supported round-start constants event.
    const event = {frame:0, type:'ige', data:{id:0, frame:0, type:'custom',
        data:{type:'constants',data:{options:{kickset:fixtures.rotation_profile}}}}};
    for (const native of [false, true]) {
        const game = create(native ? [event,...fixtures.i_spin_double] : fixtures.i_spin_double);
        const state = game.ejectState();
        Object.assign(state.game.falling, {type:'i',x:4,y:17.96,r:0,hy:18});
        const rows=['...j......','...jjj..oo','.....t..oo','.....tt..j','loot.tlllj','####.#####'];
        for(let y=0;y<6;++y) for(let x=0;x<10;++x)
            state.game.board[34+y][x]=rows[y][x]==='.' ? null : 'gb';
        game.injectState(state);
        game.doFrame();
        assert.equal(game.state.stats.piecesplaced,1);
        assert.equal(game.state.stats.lines,native ? 2 : 0);
        if(native) assert.equal(game.state.stats.clears.minitspindoubles,1);
        ++checked;
    }
}
{
    const bad = structuredClone(fixtures.single);
    bad[1].data.subframe = bad[0].data.subframe;
    const game = create(bad);
    const before = Math.ceil(game.state.falling.y);
    game.doFrame();
    assert.equal(Math.ceil(game.state.falling.y), before); // zero-length pulse: 0 cells
    ++checked;
}
{
    const game = create(fixtures.single, 41);
    const state = game.ejectState();
    state.game.falling.y = 18.5;
    game.injectState(state);
    const before = Math.ceil(game.state.falling.y);
    game.doFrame();
    assert.notEqual(Math.ceil(game.state.falling.y), before + 1);
    ++checked;
}
console.log(`Official TETR.IO v19: ${checked} movement conformance checks passed`);
