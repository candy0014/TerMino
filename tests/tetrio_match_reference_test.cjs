'use strict';
// Transport/schema regression only. Repeating a DropBot fixture is NOT a BOT
// benchmark or a naturally played seven-round match.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {verify}=require('../tools/verify_tetrio_match.cjs');
const {finalize}=require('../tools/finalize_tetrio_match.cjs');
const {loadReference}=require('../tools/tetrio_engine_reference.cjs');
const [bundle,fixture]=process.argv.slice(2);
if (!bundle || !fixture) throw Error('Usage: node tests/tetrio_match_reference_test.cjs <official tetrio.js> <fixture.ttrm>');
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'tetrio-match-test-'));
const filename=path.join(temp,'fixture.ttrm');
const audit=fs.readFileSync(path.join(path.dirname(fixture),'round_1.audit.json'));
const original=JSON.parse(fs.readFileSync(fixture));
const save=data=>fs.writeFileSync(filename,JSON.stringify(data));
try {
    fs.writeFileSync(path.join(temp,'round_1.audit.json'),audit);
    save(original);
    assert.equal(verify(bundle,filename).placements,24);
    assert.throws(()=>verify(bundle,filename,{requireComplete:true}),/seven wins/);
    const bad=structuredClone(original);
    bad.replay.rounds[0][0].replay.events=bad.replay.rounds[0][0].replay.events.filter(
        e=>!(e.type.startsWith('key') && e.data.key==='hardDrop'));
    save(bad);
    assert.throws(()=>verify(bundle,filename),/piece 1 pieces/);
    const wrongWinner=structuredClone(original);
    wrongWinner.replay.rounds[0][0].replay.results.gameoverreason='winner';
    wrongWinner.replay.rounds[0][1].replay.results.gameoverreason='winner';
    save(wrongWinner);
    assert.throws(()=>verify(bundle,filename),/one winner/);
    const seven=structuredClone(original);
    seven.replay.rounds=Array.from({length:7},()=>structuredClone(original.replay.rounds[0]));
    for (const entry of seven.replay.leaderboard) entry.wins*=7;
    for (let r=2;r<=7;++r) fs.writeFileSync(path.join(temp,`round_${r}.audit.json`),audit);
    save(seven);
    assert.equal(verify(bundle,filename,{requireComplete:true}).placements,168);
    const output=finalize(bundle,filename,path.join(temp,'checked'));
    assert.equal(path.basename(output),'match.ttrm');
    const result=JSON.parse(fs.readFileSync(output));
    assert.equal(result.replay.rounds[0][0].replay.results.stats.finesse.perfectpieces,12);
    assert.ok(result.replay.rounds[0][0].replay.results.native_stats);
    assert.deepEqual(result.replay.rounds[0][0].replay.events,seven.replay.rounds[0][0].replay.events);
    assert.throws(()=>finalize(bundle,filename,path.join(temp,'checked')),/must be new/);
    const rotationEvent=original.replay.rounds[0][0].replay.events.find(
        e=>e.type==='ige' && e.data.type==='custom' && e.data.data.type==='constants');
    assert.equal(rotationEvent,undefined,'standard rotations need no constants override');
    assert.equal(original.replay.rounds[0][0].replay.options.kickset,'SRS+');
    const profileFile=path.join(temp,'rotation.json');
    fs.writeFileSync(profileFile,JSON.stringify(loadReference(bundle).constants.kicksets['SRS+']));
    const legacy=structuredClone(seven);
    for(const entry of legacy.replay.rounds.flat()) entry.replay.events=entry.replay.events.filter(
        e=>!(e.type==='ige' && e.data.type==='custom' && e.data.data.type==='constants'));
    save(legacy);
    const migrated=JSON.parse(fs.readFileSync(finalize(bundle,filename,path.join(temp,'migrated'),false,profileFile)));
    for(const [r,sides] of migrated.replay.rounds.entries()) for(const [s,entry] of sides.entries()) {
        assert.deepEqual(entry.replay.events.filter(e=>e.type.startsWith('key')),
            legacy.replay.rounds[r][s].replay.events.filter(e=>e.type.startsWith('key')));
        assert.equal(entry.replay.events.filter(e=>e.type==='ige' && e.data.type==='custom' &&
            e.data.data.type==='constants').length,1);
    }
    assert.deepEqual(JSON.parse(fs.readFileSync(filename)),legacy,'migration must not overwrite input');
    save(migrated);
    assert.throws(()=>finalize(bundle,filename,path.join(temp,'duplicate'),false,profileFile),/already contains/);
    seven.replay.rounds.push(structuredClone(original.replay.rounds[0]));
    for (const entry of seven.replay.leaderboard) if(entry.wins) ++entry.wins;
    save(seven);
    assert.throws(()=>verify(bundle,filename),/continued after seven/);
    console.log('Official TETR.IO match transport, standard rotations and legacy profile migration passed');
} catch(e) {
    console.error(e.stack.split('\n').filter(line=>line.length<1000).join('\n'));
    process.exitCode=1;
} finally {
    // Only the exact directory allocated by this test is removed.
    fs.rmSync(temp,{recursive:true});
}
