'use strict';
const assert=require('node:assert/strict');
const {execFileSync}=require('node:child_process');
const {loadReference}=require('../tools/tetrio_engine_reference.cjs');
if(process.argv.length!==4) throw Error('Usage: node tests/garbage_rise_reference_test.cjs <official tetrio.js> <garbage_rise_test>');
const engine=loadReference(process.argv[2]);
const fixtures=JSON.parse(execFileSync(process.argv[3],['--fixtures'],{encoding:'utf8'}));
const altitudes=[0,75,225,375,550,750,975,1225,1500,1800,3000,6000,10000];
for(const altitude of altitudes) for(let expert=0;expert<3;++expert) {
    const game=engine.create('replay',{replay:new engine.Replay({frames:30,events:[
        {frame:0,type:'keydown',data:{key:'hardDrop',subframe:.5}},
        {frame:1,type:'keyup',data:{key:'hardDrop',subframe:0}}
    ]})});
    game.setHeadless(true);
    game.setGame({version:19,seed:1,g:0,gincrease:0,zenith:true,hasgarbage:true,
        garbagespeed:0,garbageentry:expert ? 'instant':'continuous',garbagecap:8,garbagecapmax:8,
        zenith_expert:expert>0,zenith_mods:expert===2 ? ['expert_reversed']:expert ? ['expert']:[],
        openerphase:0,garbageblocking:'combo blocking',garbagemultiplier:1,receivemultiplier:1,cancelmultiplier:1,
        handling:{arr:0,das:4,dcd:0,sdf:20,safelock:false,cancel:false,may20g:false,irs:'off',ihs:'off'}});
    game.startGame();
    assert.equal(game.options.garbageare,5);
    assert.equal(game.options.garbagearebump,12);
    const state=game.ejectState();
    state.game.stats.zenith.altitude=altitude;
    state.game.impendingdamage=[{id:1,type:'garbage',amt:4,status:'spawn',active:true,column:null,delay:0,queued:true}];
    game.injectState(state);
    const rows=[];
    for(let frame=1;frame<=27;++frame) {
        game.doFrame(); rows.push(game.state.board.filter(row=>row.includes('gb')).length);
        assert.equal(game.options.garbageare,5,`row interval at ${altitude}m`);
        assert.equal(game.options.garbagearebump,12,`hesitation at ${altitude}m`);
        const floor=game.state.stats.zenith.floor;
        assert.equal(game.options.garbagephase,expert ? 66-6*floor:165-15*floor,`buffer delay at ${altitude}m`);
    }
    assert.deepEqual(fixtures.rows[expert],rows,`QP2 ${['normal','EX','rEX'][expert]} row timing at ${altitude}m`);
}
console.log(`Official TETR.IO v19: fixed QP2 row timing and floor-dependent buffer delays passed at ${altitudes.length} altitudes (1053 frames)`);
