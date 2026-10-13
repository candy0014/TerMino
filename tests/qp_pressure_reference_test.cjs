'use strict';
const assert=require('node:assert/strict');
const {execFileSync}=require('node:child_process');
const {loadReference}=require('../tools/tetrio_engine_reference.cjs');
if(process.argv.length!==4) throw Error('Usage: node tests/qp_pressure_reference_test.cjs <official tetrio.js> <qp_pressure_test>');
const engine=loadReference(process.argv[2]);
const fixtures=JSON.parse(execFileSync(process.argv[3],['--fixtures'],{encoding:'utf8'}));
function packet(amount) {
    return {id:1,type:'garbage',amt:amount,status:'spawn',active:true,column:null,
        delay:0,queued:true,cid:null,gameid:null};
}
for(const f of fixtures) {
    const game=engine.create('replay',{replay:new engine.Replay({frames:600,events:[]})});
    game.setHeadless(true);
    game.setGame({version:19,seed:1,g:0,gincrease:0,zenith:true,hasgarbage:true,
        zenith_volatile:!!f.vl,zenith_mods:f.vl ? ['volatile']:[],
        garbageentry:'continuous',garbagecap:8,garbagecapmax:8,garbagespeed:0,
        openerphase:0,garbagemultiplier:1,receivemultiplier:1,cancelmultiplier:1,
        handling:{arr:0,das:4,dcd:0,sdf:20,safelock:false,cancel:false,may20g:false,irs:'off',ihs:'off'}});
    game.startGame();
    let state=game.ejectState();
    state.game.stats.zenith.altitude=f.altitude;
    state.game.hesitatedattacks=[packet(f.amount)];
    game.injectState(state); game.doFrame();
    // The public engine advances its frame before post-frame reception. The
    // first injection is processed in frame 1; normalize that reception to t=0.
    while(game.getFrame()<12) game.doFrame();
    state=game.ejectState(); state.game.hesitatedattacks=[packet(f.amount)];
    game.injectState(state); game.doFrame();
    assert.equal(game.state.windupwaituntil ? (game.state.windupwaituntil-1)/60:0,f.until,JSON.stringify(f));
    const segments=[];
    // Collect scheduled segments from both started and queued Windups.
    for(const event of game.state.waitingframes) {
        if(event.type==='process-exploded-attack') segments.push([event.data.amt,(event.target-1)/60]);
        if(event.type==='start-explode-attack') for(let i=0;i<event.data.length;++i)
            segments.push([event.data[i].amt,(event.target-1)/60+1+.5*i]);
    }
    assert.deepEqual(segments,f.segments,JSON.stringify(f));
}
console.log(`Official TETR.IO v19: ${fixtures.length} Windup threshold, capacity, altitude and serialized timing cases passed`);
