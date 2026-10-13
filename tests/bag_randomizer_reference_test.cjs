'use strict';
const assert=require('node:assert/strict');
const {execFileSync}=require('node:child_process');
const {loadReference}=require('../tools/tetrio_engine_reference.cjs');
if(process.argv.length!==4) {console.error('Usage: node tests/bag_randomizer_reference_test.cjs <tetrio.js> <bag_randomizer_test>');process.exit(2);}
const engine=loadReference(process.argv[2]);
const fixtures=JSON.parse(execFileSync(process.argv[3],['--fixtures'],{encoding:'utf8'}));
let checked=0,bags=0;
for(const fixture of fixtures) {
    const expected=fixture.bags.flat(),events=[];
    for(let i=0;i<expected.length;++i)events.push(
        {frame:2*i,type:'keydown',data:{key:'hardDrop',subframe:0}},
        {frame:2*i+1,type:'keyup',data:{key:'hardDrop',subframe:0}});
    const game=engine.create('replay',{replay:new engine.Replay({frames:events.length+1,events})});
    game.setHeadless(true);game.setGame({version:19,seed:fixture.seed,seed_random:false,g:0,gincrease:0,
        bagtype:fixture.mode,are:0,lineclear_are:0,nolockout:true,
        handling:{arr:0,das:4,dcd:0,sdf:20,safelock:false,cancel:false,may20g:false,irs:'off',ihs:'off'}});
    game.startGame();
    for(let i=0;i<expected.length;++i) {
        assert.equal(game.state.falling.type,expected[i],`${fixture.mode}, seed ${fixture.seed}, piece ${i}`);
        const state=game.ejectState();for(const row of state.game.board)row.fill(null);
        game.injectState(state);game.doFrame();game.doFrame();
        assert.equal(game.state.stats.piecesplaced,i+1);
        ++checked;
    }
    bags+=fixture.bags.length;
}
console.log(`Official TETR.IO v19: ${checked} pieces in ${bags} bags match across all 8 randomizers`);
