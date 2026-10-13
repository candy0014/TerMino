'use strict';
const assert=require('node:assert/strict');
const {execFileSync}=require('node:child_process');
const {loadReference}=require('../tools/tetrio_engine_reference.cjs');
if(process.argv.length!==4) {
    console.error('Usage: node tests/rotation_systems_reference_test.cjs <tetrio.js> <rotation_systems_test>');process.exit(2);
}
const engine=loadReference(process.argv[2]);
const fixtures=JSON.parse(execFileSync(process.argv[3],['--fixtures'],{encoding:'utf8',maxBuffer:16*1024*1024}));
const games=new Map();
let checked=0,kicks=0;
for(const [i,fixture] of fixtures.entries()) {
    const key=fixture.system+fixture.turn;
    if(!games.has(key)) {
        const game=engine.create('replay',{replay:new engine.Replay({frames:10,events:[
            {frame:0,type:'keydown',data:{key:['','rotateCW','rotate180','rotateCCW'][fixture.turn],subframe:0}}
        ]})});
        game.setHeadless(true);game.setGame({version:19,seed:1,g:0,gincrease:0,allow180:true,kickset:fixture.system,
            spinbonuses:'all-mini+',nolockout:true,infinite_movement:true,
            handling:{arr:0,das:4,dcd:0,sdf:20,safelock:false,cancel:false,may20g:false,irs:'off',ihs:'off'}});
        game.startGame();games.set(key,{game,baseline:game.ejectState()});
    }
    const {game,baseline}=games.get(key);
    const state=structuredClone(baseline);
    Object.assign(state.game.falling,{type:fixture.type,x:fixture.col,y:fixture.row-0.04,r:fixture.from,hy:fixture.row});
    for(const [x,y] of fixture.cells)state.game.board[y][x]='gb';
    game.injectState(state);game.doFrame();
    const falling=game.state.falling;
    const tag=`${fixture.system} ${fixture.type} ${fixture.from}->${(fixture.from+fixture.turn)%4}, fixture ${i}`;
    assert.equal(Boolean(falling.flags&7),fixture.success,tag+' success');
    assert.deepEqual([falling.x,Math.ceil(falling.y),falling.r],fixture.result.slice(0,3),tag+' position');
    if(fixture.success)assert.equal(falling.kick,Math.max(0,fixture.result[3]-1),tag+' kick order');
    assert.equal(falling.flags & 8 ? falling.flags & 16 ? 2 : 1 : 0,fixture.spin,tag+' spin classification');
    kicks+=fixture.success && fixture.result[3]>0; ++checked;
}
console.log(`Official TETR.IO v19: ${checked} rotation conformance checks passed (${kicks} wall kicks)`);
