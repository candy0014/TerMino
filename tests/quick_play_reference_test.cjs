// Optional offline audit. The official client is supplied externally and is
// hash-locked by the existing oracle; none of its engine code is distributed.
'use strict';
const assert=require('node:assert/strict');
const {execFileSync}=require('node:child_process');
const {loadReference,reviewedSha256}=require('../tools/tetrio_engine_reference.cjs');
if(process.argv.length!==4)throw Error('Usage: node tests/quick_play_reference_test.cjs <official tetrio.js> <tetr_rules_tests>');
const reference=loadReference(process.argv[2]);
const fixtures=JSON.parse(execFileSync(process.argv[3],['quick_play_rules_test','--audit-fixtures'],{encoding:'utf8',maxBuffer:8*1024*1024}));
function game(mod='',expert=false) {
    const g=reference.create('replay',{replay:new reference.Replay({frames:1200,events:[]})});
    g.setHeadless(true);
    g.setGame({version:19,seed:1,zenith:true,g:0,gincrease:0,hasgarbage:true,
        zenith_expert:expert,zenith_mods:mod?[mod]:[],
        zenith_messy:mod.startsWith('messy'),
        handling:{das:4,arr:0,sdf:20,irs:'off',ihs:'off'}});
    g.startGame();
    const snapshot=g.ejectState();snapshot.frame=1;g.injectState(snapshot);
    return g;
}
function close(actual,expected,description) {
    assert.ok(Math.abs(actual-expected)<1e-8,`${description}: TerMino ${actual}, official ${expected}`);
}
let count=0;
for(const f of fixtures.booster) {
    const g=game(f.expert===2?'expert_reversed':f.expert?'expert':'',f.expert>0);
    const s=g.state;
    Object.assign(s.zenith,{climb_pts:f.xp,rank_locked_until:f.protection*60,
        promotion_fatigue:f.fatigue,bonusremaining:f.pending});
    Object.assign(s.stats.zenith,{rank:f.level,altitude:f.altitude});
    g.source.self.zthm.Update();
    const result=[Math.floor(s.stats.zenith.rank),s.zenith.climb_pts,s.stats.zenith.altitude,
        s.zenith.bonusremaining,Math.max(0,(s.zenith.rank_locked_until-1)/60),s.zenith.promotion_fatigue];
    f.result.forEach((value,i)=>close(value,result[i],`booster ${JSON.stringify(f)} field ${i}`));
    ++count;
}
for(const f of fixtures.awards) {
    const g=game(['','expert','expert_reversed','allspin_reversed'][f.mode],f.mode===1||f.mode===2);
    Object.assign(g.state.stats.zenith,{rank:2,altitude:f.altitude});
    g.state.stats.garbage.cleared=f.dug;
    g.state.zenith.bonusremaining=.5;
    // Exercise the original lock path order. Cancellation is excluded here:
    // community documentation and this public client disagree on its XP.
    if(f.lines && !(f.mode===1||f.mode===2))g.source.self.zthm.AwardLines(Math.min(f.lines,2),false);
    if(f.outgoing)g.source.self.zthm.AwardLines(f.outgoing,true);
    close(f.result[0],g.state.zenith.climb_pts,`award XP ${JSON.stringify(f)}`);
    close(f.result[1],g.state.zenith.bonusremaining,`award altitude ${JSON.stringify(f)}`);
    ++count;
}
for(const [mode,floor,grace,expected] of fixtures.center) {
    const mod=['','expert','expert_reversed','messy','messy_reversed','volatile_reversed','allspin_reversed','expert_reversed'][mode];
    const g=game(mod,mode===1||mode===2||mode===7);
    g.state.stats.zenith.altitude=[0,50,150,300,450,650,850,1100,1350,1650][floor-1]+10;
    g.state.stats.zenith.targetinggrace=grace;
    if(mode===7)g.state.zenith.maxmessy=true; // Original fatigue event at 11 minutes.
    g.source.self.zthm.Update();
    assert.equal(Boolean(expected),g.options.messiness_center,`center mode ${mode}, floor ${floor}, grace ${grace}`);
    ++count;
}
const fatigue=[reference.constants.zenithFatigue,reference.constants.zenithFatigueRevEx]
    .map(events=>events.flatMap(([frame,actions])=>actions.filter(action=>action[0]==='unclearable').map(()=>frame)));
for(const [expert,frame,expected] of fixtures.fatigue) {
    assert.equal(expected,fatigue[expert].filter(due=>due<=frame).length,`fatigue mode ${expert}, frame ${frame}`);
    ++count;
}
// Check the ranking against the original hole chooser, including multi-hole
// garbage, a lower empty row, and an empty field. Controlled random numbers
// break ties by column, then scan quantiles across the weighted distribution.
for(const f of fixtures.holes)for(const favor of [-30,0,30])for(const center of [false,true]) {
    const g=game();
    const self=g.source.self;
    f.rows.forEach((row,y)=>{g.state.board[self.T-8+y]=[...row].map(c=>c==='.'?null:c==='g'?'gb':'i');});
    Object.assign(g.options,{garbagefavor:favor,messiness_center:center,messiness_nosame:false});
    const ranked=f.scores.map((score,column)=>({score,column})).sort((a,b)=>a.score-b.score||a.column-b.column);
    const weights=ranked.map(({column},rank)=>center&&(column<2||column>=8)?0:Math.max(0,10+favor*(1-2*rank/9)));
    const total=weights.reduce((a,b)=>a+b,0);
    for(let q=0;q<100;++q) {
        const quantile=(q+.5)/100;
        let call=0;
        const rng=g.state.rngex;
        const original=rng.nextFloat;
        rng.nextFloat=()=>favor!==0 && call++<10 ? 0:quantile;
        let actual;
        try {actual=self.atm.RerollColumn();}finally {rng.nextFloat=original;}
        let expected;
        if(favor===0)expected=(center?2:0)+Math.floor(quantile*(center?6:10));
        else {
            let cumulative=0;
            for(let i=0;i<ranked.length;++i)if((cumulative+=weights[i]) && quantile*total<=cumulative) {
                expected=ranked[i].column;break;
            }
        }
        assert.equal(actual,expected,`hole ${JSON.stringify(f.rows)}, favor ${favor}, center ${center}, q ${quantile}`);
        ++count;
    }
}
console.log(`Official TETR.IO ${reviewedSha256}: ${count} QP comparisons passed (booster, awards, center, fatigue, hole ranking).`);
