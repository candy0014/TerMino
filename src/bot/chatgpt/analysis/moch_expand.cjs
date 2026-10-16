#!/usr/bin/env node
'use strict';

// Extract only the inventory's unobserved Moch rounds. All replay execution and
// cumulative-stat verification remain in the reviewed moch_terrain extractor.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {extract, summarize} = require('./moch_terrain.cjs');
const {loadReference, reviewedSha256} = require('../../../../tools/tetrio_engine_reference.cjs');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const write = (file, data) => fs.writeFileSync(file, JSON.stringify(data, null, 2)+'\n');

const [bundle, inventoryPath, replayDir, outputDir, resumeFlag] = process.argv.slice(2);
if(!bundle || !inventoryPath || !replayDir || !outputDir)
    throw Error('Usage: moch_expand.cjs <reviewed bundle> <inventory.json> <replay dir> <output dir> [--resume]');
if(resumeFlag && resumeFlag!=='--resume')throw Error('Unknown option');
if(fs.existsSync(outputDir) && !resumeFlag)throw Error('Use a fresh output directory or --resume');
const inventory = JSON.parse(fs.readFileSync(inventoryPath, 'utf8'));
const selectedFiles = inventory.inventory.filter(f=>f.remaining_rounds>0);
for(const file of selectedFiles) {
    if(hash(path.join(replayDir,file.file))!==file.sha256)
        throw Error('Replay does not match inventory hash: '+file.file);
    for(const round of file.rounds.filter(r=>!r.extracted))
        if(round.replay_version!==19 || round.spinbonuses!=='all-mini+')
            throw Error('Unsupported inventory replay rules: '+file.file);
}
const reference = loadReference(bundle);
fs.mkdirSync(outputDir, {recursive:true});
const manifestPath = path.join(outputDir,'manifest.json');
const manifest = resumeFlag ? JSON.parse(fs.readFileSync(manifestPath,'utf8')) : {
    status:'running', reviewedSha256,
    source_inventory:{path:inventoryPath,sha256:hash(inventoryPath)},
    extractor_sha256:hash(path.join(__dirname,'moch_terrain.cjs')),
    expansion_tool_sha256:hash(__filename),
    scope:'Only inventory rounds marked extracted=false, only the inventoried Moch player; no training, native enumeration or holdout designation.',
    timing:'before is lock-time after HOLD and movement; visible_next is current plus five visible previews. Legacy next contains raw extra bag data and is not a policy input.',
    end_stat_validation:'Every replay.results.stats field except finaltime is compared by the official engine; finaltime can remain unset for winner events.',
    expected_rounds:inventory.totals.remaining_rounds,
    expected_pieces:inventory.totals.remaining_pieces,
    completed_rounds:0, completed_pieces:0, files:[], failures:[], exclusions:[],
};
if(resumeFlag) {
    if(manifest.source_inventory.sha256!==hash(inventoryPath) || manifest.reviewedSha256!==reviewedSha256)
        throw Error('Resume provenance mismatch');
    for(const file of manifest.files)
        if(hash(path.join(outputDir,file.artifact))!==file.artifact_sha256)
            throw Error('Previously verified artifact changed');
    manifest.previous_attempt_failures=[...(manifest.previous_attempt_failures||[]),...manifest.failures];
    manifest.failures=[];
    manifest.status='running';
    manifest.expansion_tool_sha256=hash(__filename);
    manifest.exclusions??=[];
}
write(manifestPath,manifest);
for(const file of selectedFiles) {
    if(manifest.files.some(f=>f.file===file.file))continue;
    const wanted = new Map(file.rounds.filter(r=>!r.extracted).map(r=>[r.round,r]));
    const raw=JSON.parse(fs.readFileSync(path.join(replayDir,file.file),'utf8'));
    for(const [round,source] of wanted) {
        const entry=raw.replay.rounds[round-1].find(e=>e.username===source.username);
        if(!entry)throw Error('Inventory player missing');
        const events=entry.replay.events;
        if(!events.some(e=>e.type==='start'&&e.frame===0)) {
            manifest.exclusions.push({file:file.file,round,pieces:source.pieces,
                reason:'missing_frame_zero_start_cannot_reconstruct_initial_board',
                first_event:events[0],start_events:events.filter(e=>e.type==='start').length,
                full_events:events.filter(e=>e.type==='full').length,
                hard_drop_keydowns:events.filter(e=>e.type==='keydown'&&e.data.key==='hardDrop').length});
            wanted.delete(round);
        }
    }
    const started=Date.now();
    try {
        const rounds=extract(reference,path.join(replayDir,file.file),Infinity,
            ({round,entry})=>wanted.has(round)&&entry.username===wanted.get(round).username);
        if(rounds.length!==wanted.size)throw Error('Round count differs from inventory');
        for(const round of rounds) {
            const source=wanted.get(round.round);
            if(round.placements.length!==source.pieces)throw Error('Placement count differs from inventory');
            if(round.options.seed!==source.seed)throw Error('Replay seed differs from inventory');
            for(const p of round.placements)for(const state of [p.before,p.after])
                if(state.preview_count!==5 || state.visible_next.length!==6 ||
                    state.visible_next.join('')!==state.next.slice(0,6).join(''))
                    throw Error('Visible current plus five preview contract failed');
        }
        const filename=path.basename(file.file,'.ttrm')+'.placements.json';
        const artifact=path.join(outputDir,filename);
        // Preserve the established export schema consumed by prepare_root.
        fs.writeFileSync(artifact,JSON.stringify({reviewedSha256,rounds})+'\n');
        const record={file:file.file,source_sha256:file.sha256,artifact:filename,
            artifact_sha256:hash(artifact),rounds:rounds.map(r=>r.round),
            pieces:rounds.reduce((n,r)=>n+r.placements.length,0),
            elapsed_seconds:(Date.now()-started)/1000,summary:summarize(rounds)};
        manifest.files.push(record);
        manifest.completed_rounds+=rounds.length;
        manifest.completed_pieces+=record.pieces;
        write(manifestPath,manifest);
        process.stdout.write('FILE_COMPLETE '+JSON.stringify({file:file.file,rounds:rounds.length,
            pieces:record.pieces,completed_rounds:manifest.completed_rounds,
            completed_pieces:manifest.completed_pieces})+'\n');
    } catch(error) {
        manifest.failures.push({file:file.file,error:String(error.stack||error)});
        write(manifestPath,manifest);
        process.stdout.write('FILE_FAILED '+JSON.stringify({file:file.file,error:String(error)})+'\n');
    }
}
manifest.excluded_rounds=manifest.exclusions.length;
manifest.excluded_pieces=manifest.exclusions.reduce((n,r)=>n+r.pieces,0);
manifest.status=manifest.failures.length?'failed':manifest.exclusions.length?'complete_with_exclusions':'complete';
if(!manifest.failures.length && (manifest.completed_rounds+manifest.excluded_rounds!==manifest.expected_rounds ||
    manifest.completed_pieces+manifest.excluded_pieces!==manifest.expected_pieces))
    throw Error('Final expansion totals differ from inventory');
write(manifestPath,manifest);
if(manifest.failures.length)process.exitCode=1;
