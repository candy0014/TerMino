'use strict';
// Optional metadata enrichment. The standalone C++ exporter needs no Node or
// client bundle. This tool verifies every native lock before deriving ancillary
// client statistics (score/finesse/etc.) with the unchanged official engine.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {verify} = require('./verify_tetrio_match.cjs');
const {reviewedSha256} = require('./tetrio_engine_reference.cjs');

function retimeDevelopmentSide(entry, side) {
    const inputs=entry.replay.events.filter(event=>event.type.startsWith('key'));
    let tick=0;
    for(let i=0;i<inputs.length;i+=2) {
        const down=inputs[i], up=inputs[i+1];
        if(down.type!=='keydown' || !up || up.type!=='keyup' || down.data.key!==up.data.key)
            throw Error('Development timing conversion requires paired taps');
        const base=15*Math.floor(down.frame/15), old=down.frame-base+down.data.subframe;
        const key=down.data.key;
        const at=key==='hold' ? (side ? 8 : 0) : key==='hardDrop' ? (side ? 14.5 : 7.5)
            : (side ? 8 : 1)+(old-7)*5/6;
        tick=Math.max(tick,Math.floor((base+at)*10000));
        // Never split a one-cell soft-drop pulse across a frame boundary.
        if(tick%10000>=9998) tick+=10000-tick%10000;
        down.frame=up.frame=Math.floor(tick/10000);
        down.data.subframe=(tick%10000)/10000;
        up.data.subframe=down.data.subframe+(key==='softDrop' ? 0.0001 : 0);
        tick+=2;
    }
    if(side===0) for(const event of entry.replay.events) {
        if(event.type!=='ige') continue;
        event.frame=Math.max(0,event.frame-7);
        event.data.frame=event.frame;
        if('frame' in event.data.data) event.data.data.frame=event.frame;
    }
    entry.replay.events.sort((a,b)=>a.frame-b.frame);
}

function addNativeRotationProfile(data, profile) {
    // This compatibility event describes the original host rules for every
    // round; never inject a board, alter a key, or patch an individual outcome.
    for (const field of ['kicks','i_kicks']) {
        for (const transition of ['01','10','12','21','23','32','30','03','02','20','13','31']) {
            const kicks=profile[field]?.[transition];
            if (!Array.isArray(kicks) || kicks.some(k=>!Array.isArray(k) || k.length!==2 ||
                k.some(v=>!Number.isInteger(v) || Math.abs(v)>10)))
                throw Error('Invalid native rotation profile');
        }
    }
    for (const entry of data.replay.rounds.flat()) {
        const events=entry.replay.events;
        if(events.some(e=>e.type==='ige' && e.data.type==='custom' && e.data.data.type==='constants'))
            throw Error('Replay already contains native constants; omit the migration profile');
        for (const event of events) if(event.type==='ige') ++event.data.id;
        const start=events.findIndex(e=>e.frame===0 && e.type==='start');
        if(start<0) throw Error('Missing round start');
        events.splice(start+1,0,{frame:0,type:'ige',data:{id:0,frame:0,type:'custom',
            data:{type:'constants',data:{options:{kickset:profile}}}}});
    }
}

function finalize(bundle, input, output, legacyTiming = false, rotationProfileFile = null) {
    if (fs.existsSync(output)) throw Error('Output directory must be new');
    const original = JSON.parse(fs.readFileSync(input,'utf8'));
    if(rotationProfileFile) addNativeRotationProfile(original,
        JSON.parse(fs.readFileSync(rotationProfileFile,'utf8')));
    // Old captures omitted host-specific kicks. Audit the corrected rule
    // description before publishing anything, against untouched native states.
    const source = verify(bundle, input, {data:original});
    const data = source.data;
    const audits = data.replay.rounds.map((_, i) => JSON.parse(fs.readFileSync(
        path.join(path.dirname(input), `round_${i + 1}.audit.json`), 'utf8')));
    if (legacyTiming) {
        // Only for the first development capture, whose two simulation clocks
        // used the same lock phase. Preserve lock/garbage timing intervals,
        // and put each side's taps in its own movement window. No keys,
        // packets, seeds, boards or decisions are added/removed/replaced.
        for (const [r, sides] of data.replay.rounds.entries()) {
            for(const [side,entry] of sides.entries()) retimeDevelopmentSide(entry,side);
            for (const snapshot of audits[r][0]) snapshot.frame -= 7;
        }
    }
    fs.mkdirSync(output, {recursive:true});
    for (const [r, audit] of audits.entries()) fs.writeFileSync(
        path.join(output, `round_${r + 1}.audit.json`), JSON.stringify(audit), {flag:'wx'});
    const candidate = path.join(output, 'native_metadata.json');
    fs.writeFileSync(candidate, JSON.stringify(data), {flag:'wx'});
    const verified = verify(bundle, candidate, {onVerifiedSide({entry, game}) {
        // Native gameplay and external winner are authoritative. Do not alter
        // input events or replace the board to make a failed check pass.
        const nativeStats = entry.replay.results.stats;
        const clientStats = structuredClone(game.state.stats);
        clientStats.finaltime ??= entry.replay.frames * 1000 / 60;
        entry.replay.results.native_stats = nativeStats;
        entry.replay.results.stats = clientStats;
        if (entry.replay.results.gameoverreason !== 'winner' && game.state.gameoverreason)
            entry.replay.results.gameoverreason = game.state.gameoverreason;
    }});
    for (let side = 0; side < 2; ++side) {
        let attack=0, pieces=0, cleared=0, sent=0, received=0, frames=0;
        for (const round of verified.data.replay.rounds) {
            const entry=round[side], s=entry.replay.results.stats;
            attack+=s.garbage.attack; pieces+=s.piecesplaced; cleared+=s.garbage.cleared;
            sent+=s.garbage.sent; received+=s.garbage.received; frames+=entry.replay.frames;
        }
        const seconds=Math.max(1/60, frames/60);
        verified.data.replay.leaderboard[side].stats = {apm:attack/seconds*60,
            pps:pieces/seconds, vsscore:(attack+cleared)/seconds*100,
            garbagesent:sent, garbagereceived:received, kills:verified.wins[side]};
    }
    const complete=Math.max(...verified.wins)===7;
    const result=path.join(output, complete ? 'match.ttrm' : 'checkpoint.ttrm');
    fs.writeFileSync(result, JSON.stringify(verified.data), {flag:'wx'});
    fs.writeFileSync(path.join(output,'provenance.json'), JSON.stringify({input:path.resolve(input),
        inputSha256:crypto.createHash('sha256').update(fs.readFileSync(input)).digest('hex'),
        officialBundleSha256:reviewedSha256,
        rotationProfileFile:rotationProfileFile && path.resolve(rotationProfileFile),
        rotationProfileSha256:rotationProfileFile && crypto.createHash('sha256')
            .update(fs.readFileSync(rotationProfileFile)).digest('hex'),
        legacyTiming, placements:verified.placements, wins:verified.wins,
        note:'Raw input file retained; every native lock verified. Official-engine ancillary statistics, with timing/rule-profile migration only when explicitly selected.'}, null, 2), {flag:'wx'});
    verify(bundle, result, {requireComplete:complete});
    return result;
}
if (require.main === module) {
    const [bundle,input,output,...flags]=process.argv.slice(2);
    try {
        let legacy=false, profile=null;
        for(let i=0;i<flags.length;++i) {
            if(flags[i]==='--legacy-simultaneous') legacy=true;
            else if(flags[i]==='--native-rotation-profile' && flags[i+1]) profile=flags[++i];
            else throw Error('Unknown/incomplete option: '+flags[i]);
        }
        if (!bundle || !input || !output) throw Error('Usage: node tools/finalize_tetrio_match.cjs <official tetrio.js> <match.ttrm> <NEW_DIRECTORY> [--legacy-simultaneous] [--native-rotation-profile profile.json]');
        console.log('Finalized:', finalize(bundle,input,output,legacy,profile));
    }
    catch (e) { console.error(e.stack.split('\n').filter(line=>line.length<1000).join('\n')); process.exitCode=1; }
}
module.exports={finalize};
