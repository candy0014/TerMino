'use strict';
const assert=require('node:assert/strict');
const {execFileSync}=require('node:child_process');
const {loadReference}=require('../tools/tetrio_engine_reference.cjs');
if(process.argv.length!==4) throw Error('Usage: node tests/timing_garbage_reference_test.cjs <official tetrio.js> <timing_garbage_rules_test>');
const engine=loadReference(process.argv[2]);
const fixtures=JSON.parse(execFileSync(process.argv[3],['--fixtures'],{encoding:'utf8'}));
function create(options={}) {
    const game=engine.create('replay',{replay:new engine.Replay({frames:30,events:[
        {frame:0,type:'keydown',data:{key:'hardDrop',subframe:.5}},
        {frame:1,type:'keyup',data:{key:'hardDrop',subframe:0}}
    ]})});
    game.setHeadless(true);
    game.setGame({version:19,seed:1,seed_random:false,g:0,gincrease:0,hasgarbage:true,
        bagtype:'7-bag',kickset:'SRS+',spinbonuses:'all-mini+',nolockout:true,
        garbageentry:'instant',garbagecap:8,garbagecapmax:8,garbagespeed:0,
        garbagemultiplier:1,receivemultiplier:1,cancelmultiplier:1,garbageholesize:1,
        messiness_change:1,messiness_inner:0,messiness_nosame:false,
        messiness_center:false,messiness_timeout:0,passthrough:'full',openerphase:0,
        are:0,lineclear_are:0,combotable:'multiplier',roundmode:'down',
        b2bchaining:false,b2bextras:false,b2bcharging:true,b2bcharge_at:4,b2bcharge_base:3,
        allclear_garbage:5,allclear_b2b:1,allclear_charges:false,allclear_b2b_sends:true,
        allclear_b2b_dupes:false,garbagespecialbonus:false,...options,
        handling:{arr:0,das:4,dcd:0,sdf:20,safelock:false,cancel:false,may20g:false,irs:'off',ihs:'off'}});
    game.startGame(); return game;
}
function setClear(state,lines,pc) {
    Object.assign(state.game.falling,{type:'i',x:4,y:36.96,r:1,hy:37});
    for(let y=40-lines;y<40;++y) for(let x=0;x<10;++x) state.game.board[y][x]=x===5 ? null : 'j';
    if(!pc) state.game.board[34][0]='j';
}
function packet(amt) {return {id:1,type:'garbage',amt,status:'spawn',active:true,column:null,delay:0,queued:true};}
let checked=0;
for(const f of fixtures.attacks) {
    const game=create({garbagemultiplier:f.multiplier});
    const state=game.ejectState();setClear(state,f.lines,f.pc);
    state.game.stats.combo=f.combo-1; state.game.stats.btb=Math.max(0,f.b2b-1);
    game.injectState(state);game.doFrame();
    assert.equal(game.state.stats.lines,f.lines,JSON.stringify(f));
    assert.equal(game.state.stats.garbage.attack,f.attack,JSON.stringify(f)); ++checked;
}
for(const f of fixtures.openers) {
    const game=create({openerphase:f.limit});
    const state=game.ejectState();setClear(state,4,f.pc);
    state.game.stats.piecesplaced=f.placed-1;state.game.stats.garbage.sent=f.sent;
    if(f.pending) state.game.impendingdamage=[packet(f.pending)];
    game.injectState(state);game.doFrame();
    assert.equal(game.state.stats.garbage.sent-f.sent,f.outgoing,'opener send '+JSON.stringify(f));
    assert.equal(game.state.impendingdamage.reduce((n,p)=>n+p.amt,0),f.remaining,'opener buffer '+JSON.stringify(f)); ++checked;
}
for(const f of fixtures.holes) {
    const game=create({seed:f.seed,messiness_inner:f.messiness,messiness_nosame:!!f.nosame,
        garbagecap:f.cap,garbagecapmax:f.cap});
    const state=game.ejectState();state.game.impendingdamage=[packet(12)];
    game.injectState(state);game.doFrame();
    const holes=Array.from(game.state.board.slice(40-f.holes.length),row=>row.indexOf(null));
    assert.deepEqual(holes,f.holes,'hole sequence '+JSON.stringify(f));
    assert.equal(game.state.rngex.state ?? game.state.rngex._seed,f.rng,'hole RNG '+JSON.stringify(f));
    assert.equal(game.state.lastcolumn,f.column<0 ? null : f.column,'hole column '+JSON.stringify(f));
    assert.equal(game.state.impendingdamage.reduce((n,p)=>n+p.amt,0),12-f.holes.length,'rise cap'); ++checked;
}
for(const entry of fixtures.are) {
    const round=entry.replay;
    assert.equal(round.options.lineclear_are,round.options.are+6,'clear delay includes ARE');
    const game=engine.create('replay',{replay:new engine.Replay(round)});
    game.setHeadless(true); game.setGame(round.options); game.startGame();
    while(game.getFrame()<round.frames) game.doFrame();
    assert.equal(game.state.stats.piecesplaced,3,'exported ARE must not suppress a hard drop'); ++checked;
}
console.log(`Official TETR.IO v19: ${checked} attack, opener and garbage-hole conformance checks passed`);
