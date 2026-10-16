'use strict';
// Selected-round observer only. Never edits the engine, inputs or old exports.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const {loadReference,reviewedSha256}=require('../../../../tools/tetrio_engine_reference.cjs');
const {snapshot}=require('./moch_terrain.cjs');
const {capturePressure}=require('./moch_pressure.cjs');
const base='runlogs/chatgpt_moch_20260926/tactical_cases_v2';
const out=path.join(base,'pressure_inputs');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const write=(name,data)=>fs.writeFileSync(path.join(out,name),JSON.stringify(data,null,2)+'\n');
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const types='ijlostz';

function replayRound(reference,entry,old) {
    const replay=new reference.Replay(entry.replay),game=reference.create('replay',{replay});
    game.setHeadless(true);game.setGame(entry.replay.options);
    const observations=new Map(),known=new Map();let pending=null;
    function observe(kind) {
        const state=game.state,s=snapshot(state),q=s.visible_next;
        delete s.next; // Never export the ordered hidden legacy preview.
        const draws=s.stats.piecesplaced+1+Number(Boolean(s.hold));
        if(!known.size) {
            if(s.hold)known.set(1,s.hold);
            known.set(draws,q[0]);
        }
        for(let i=1;i<q.length;++i) {
            const ordinal=draws+i;
            if(known.has(ordinal)&&known.get(ordinal)!==q[i])throw Error('Visible draw conflict');
            known.set(ordinal,q[i]);
        }
        const end=draws+5,start=Math.floor((end-1)/7)*7+1,prefix=[];
        for(let i=start;i<=end;++i) {if(!known.has(i))throw Error('Unobserved endpoint prefix');prefix.push(known.get(i));}
        if(new Set(prefix).size!==prefix.length)throw Error('Endpoint bag duplicate');
        const f=state.falling;
        return {kind,frame:game.getFrame(),subframe:state.subframe,snapshot:s,
            rule_context:{garbagemultiplier:state.setoptions.garbagemultiplier,
                garbagemargin:state.setoptions.garbagemargin,garbageincrease:state.setoptions.garbageincrease},
            audit:{holds:state.stats.holds,inputs:state.stats.inputs,holdlocked:state.holdlocked,
                falling:{type:f.type,x:f.x,y:f.y,r:f.r,flags:f.flags,keys:f.keys},playing:game.isPlaying()},
            unseen:{types:[...types].filter(c=>!prefix.includes(c)),draws,endpoint:end,bag_start:start,
                provenance:'Only earlier/current visible previews observed in this replay up to this callback'},
            pressure:capturePressure(state,game.getFrame())};
    }
    function record(index) {if(!observations.has(index))observations.set(index,{index,spawns:[]});return observations.get(index);}
    function finish() {
        if(!pending)return;
        const p=pending;pending=null;p.after=observe('after_lock');
        if(p.after.snapshot.stats.piecesplaced!==p.before.snapshot.stats.piecesplaced+1)throw Error('Lock count mismatch');
    }
    game.on('piece',()=>{finish();const index=game.state.stats.piecesplaced+1;record(index).spawns.push(observe('piece_event'));});
    game.on('lock',piece=>{
        if(pending)throw Error('Unfinished lock');
        const index=game.state.stats.piecesplaced+1,r=record(index);
        r.before=observe('lock_before');r.piece={type:piece.type,x:piece.x,y:piece.y,r:piece.r,kick:piece.kick,flags:piece.flags};pending=r;
    });
    const originalEmit=game.source.Emit;
    const actions=new Set(['moveLeft','moveRight','rotateCW','rotateCCW','rotate180','hold','softDrop','hardDrop','sonicDrop']);
    game.source.Emit=function(event) {
        if(event.type==='keydown'&&actions.has(event.data?.key)) {
            const index=game.state.stats.piecesplaced+1,r=record(index);
            if(!r.first_action)r.first_action={...observe('before_first_action_keydown'),event_key:event.data.key};
        }
        return originalEmit.call(this,event);
    };
    game.startGame();
    for(let f=0;f<=replay.frame;++f){game.doFrame();finish();}
    const checked=[];
    for(const key of Object.keys(entry.replay.results.stats).filter(k=>k!=='finaltime')) {
        if(!equal(game.state.stats[key],entry.replay.results.stats[key]))throw Error('Final stat mismatch '+key);
        checked.push(key);
    }
    const placements=[...observations.values()].filter(p=>p.before);
    if(placements.length!==old.placements.length)throw Error('Placement total mismatch');
    for(const p of placements) {
        const original=old.placements[p.index-1];
        if(!equal(p.piece,original.piece)||p.before.frame!==original.frame)throw Error('Lock identity mismatch');
        for(const stage of ['before','after']) {
            const s=p[stage].snapshot,t=original[stage];
            for(const key of ['rows','stats','hold','pending'])if(!equal(s[key],t[key]))throw Error(`Original ${stage}.${key} mismatch at ${p.index}`);
            if(!equal(s.visible_next,t.next.slice(0,6)))throw Error('Visible queue mismatch');
        }
    }
    return {file:old.file,round:old.round,side:old.side,username:old.username,
        validation:'all original lock identity/frames/boards/stats/HOLD/visible queue/pending matched; all final cumulative stats matched',
        checked_stats:checked,placements,effective_options:{bagtype:game.state.setoptions.bagtype,nextcount:game.state.setoptions.nextcount,
            infinite_hold:game.state.setoptions.infinite_hold,garbageentry:game.state.setoptions.garbageentry,
            garbagephase:game.state.setoptions.garbagephase,garbagequeue:game.state.setoptions.garbagequeue,
            garbagecap:game.state.setoptions.garbagecap,garbagecapmax:game.state.setoptions.garbagecapmax}};
}

function main() {
    if(process.argv.length!==3)throw Error('usage: node moch_tactical_extract.cjs REVIEWED_REFERENCE');
    fs.mkdirSync(out);
    write('preregistration.json',{scope:'Six already selected rounds only; old artifacts unchanged',
        capture:'Wrap source.Emit BEFORE original keydown handler; first action per piecesplaced+1; piece-event snapshots only cross-check spawn/IHS/IRS',
        clock:'game.getFrame(), recorded state.subframe; never read imminent event subframe as current state',
        pressure:'capturePressure static FIFO-compatible schedule only; no packet holes or future events in exported inputs',
        unseen:'online observed previews only, seven-bag endpoint complement, never raw hidden bag order',
        validation:'all locks and before/after boards/stats/visible queue/pending against frozen106 exports plus official final cumulative stats',
        bot_comparison:'later baseline-only cold100ms once per eligible case root; fixed forecast step1; preserve all ineligible roots'});
    const manifest=JSON.parse(fs.readFileSync(path.join(base,'case_manifest.json'),'utf8'));
    const targets=new Set(manifest.selected.map(c=>c.source+':'+c.round)),originals=[];
    for(const s of manifest.source_exports) {
        if(hash(s.path)!==s.sha256)throw Error('Frozen source hash changed');
        originals.push(...JSON.parse(fs.readFileSync(s.path,'utf8')).rounds.filter(r=>r.username==='mochbot-dev'&&targets.has(r.file+':'+r.round)));
    }
    if(originals.length!==6)throw Error('Expected six unique source rounds');
    const reference=loadReference(process.argv[2]),reports=[];
    for(const old of originals) {
        const file=path.join('mochbot_tetrio_replay',old.file),data=JSON.parse(fs.readFileSync(file,'utf8'));
        const entry=data.replay.rounds[old.round-1][old.side];if(entry.username!==old.username)throw Error('Player mismatch');
        const result=replayRound(reference,entry,old),name=old.file.replace('.ttrm','')+'_r'+old.round+'.json';
        write(name,result);reports.push({file:old.file,round:old.round,output:name,placements:result.placements.length,validation:result.validation,source_sha256:hash(file)});
        console.log(JSON.stringify(reports.at(-1)));
    }
    write('manifest.json',{reference_sha256:reviewedSha256,collector_sha256:hash(__filename),pressure_helper_sha256:hash(path.join(__dirname,'moch_pressure.cjs')),rounds:reports});
}
if (require.main === module) main();
module.exports = {replayRound};
