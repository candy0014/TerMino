'use strict';
const assert = require('node:assert/strict');
const {execFileSync} = require('node:child_process');
const {loadReference} = require('../tools/tetrio_engine_reference.cjs');
if(process.argv.length !== 4) {
    console.error('Usage: node tests/spin_bonuses_reference_test.cjs <tetrio.js> <spin_bonuses_test>');
    process.exit(2);
}
const engine = loadReference(process.argv[2]);
const fixtures = JSON.parse(execFileSync(process.argv[3],['--fixtures'],{encoding:'utf8'}));
const modes=['T-spins','T-spins+','all','all+','all-mini','all-mini+','mini-only','handheld','stupid','none'];
let checked=0;
for(let m=0;m<modes.length;++m) {
    const game=engine.create('replay',{replay:new engine.Replay({frames:10,events:[
        {frame:0,type:'keydown',data:{key:'rotateCW',subframe:0}}
    ]})});
    game.setHeadless(true);
    game.setGame({version:19,seed:1,g:0,gincrease:0,allow180:true,spinbonuses:modes[m],nolockout:true,
        handling:{arr:0,das:4,dcd:0,sdf:20,safelock:false,cancel:false,may20g:false,irs:'off',ihs:'off'}});
    game.startGame();
    const baseline=game.ejectState();
    for(const [i,fixture] of fixtures.entries()) {
        const state=structuredClone(baseline);
        Object.assign(state.game.falling,{type:fixture.type,x:5,y:29.96,r:(fixture.r+3)%4,hy:30});
        for(const [x,y] of fixture.cells) state.game.board[y][x]='gb';
        game.injectState(state); game.doFrame();
        const falling=game.state.falling;
        assert.equal(falling.x,5); assert.equal(Math.ceil(falling.y),30); assert.equal(falling.r,fixture.r);
        const spin=falling.flags & 8 ? falling.flags & 16 ? 2 : 1 : 0;
        assert.equal(fixture.results[m],spin,`${modes[m]} ${fixture.type} r=${fixture.r}, fixture ${i}`);
        ++checked;
    }
}
console.log(`Official TETR.IO v19: ${checked} spin classification conformance checks passed`);
