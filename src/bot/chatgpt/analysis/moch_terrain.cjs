#!/usr/bin/env node
'use strict';

// Offline observation only: run the reviewed, caller-supplied official engine.
// No model of mochbot's private evaluator or search is assumed here.
// The lock-event "before" state is AFTER any HOLD and movement inputs. It is
// not a decision root and must not be used to learn whether to HOLD. Legacy
// `next` contains current + up to six raw bag entries; only `visible_next`
// respects the replay's preview count (current + five in the audited files).
const fs = require('node:fs');
const path = require('node:path');
const {loadReference, reviewedSha256} = require('../../../../tools/tetrio_engine_reference.cjs');

function terrain(rows) {
    const H = rows.length, W = rows[0].length;
    const occupied = (x,y) => rows[y][x] !== '.';
    const heights = Array(W).fill(0), holeCells = [], garbageChannels = [];
    for (let x=0; x<W; ++x) {
        let roof=0, top=H;
        for (let y=0; y<H; ++y) {
            if (occupied(x,y)) { ++roof; top=Math.min(top,y); }
            else if (roof) holeCells.push({x,y,cover:roof,depth:y-top});
        }
        heights[x]=H-top;
    }
    const reachable = new Set(), queue=[];
    for (let x=0;x<W;++x) if(!occupied(x,0)) {queue.push([x,0]);reachable.add(x);}
    for (let i=0;i<queue.length;++i) {
        const [x,y]=queue[i];
        for(const [a,b] of [[x-1,y],[x+1,y],[x,y-1],[x,y+1]]) {
            const key=b*W+a;
            if(a>=0&&a<W&&b>=0&&b<H&&!occupied(a,b)&&!reachable.has(key)) {
                reachable.add(key);queue.push([a,b]);
            }
        }
    }
    const wells=heights.map((h,x)=>Math.max(0,Math.min(x?heights[x-1]:H,x+1<W?heights[x+1]:H)-h));
    let garbageRows=0;
    for (let y=0;y<H;++y) {
        const garbage=rows[y].split('').filter(v=>v==='#').length;
        if(garbage)++garbageRows;
        if(garbage>=W-2) for(let x=0;x<W;++x) if(!occupied(x,y)) {
            let cover=0;
            for(let yy=0;yy<y;++yy) if(occupied(x,yy))++cover;
            garbageChannels.push({x,y,cover});
        }
    }
    const surfaceHoles=holeCells.filter(h=>!rows[h.y].includes('#'));
    const lowColumns=heights.map((h,x)=>h<=Math.min(...heights)+2?x:null).filter(x=>x!==null);
    return {
        heights, maxHeight:Math.max(...heights), meanHeight:heights.reduce((a,b)=>a+b,0)/W,
        bumpiness:heights.slice(1).reduce((a,h,x)=>a+Math.abs(h-heights[x]),0),
        wells, maxWell:Math.max(...wells), wellColumns:wells.map((d,x)=>d>=2?x:null).filter(x=>x!==null),
        deepWellCount:wells.filter(d=>d>=3).length,
        lowColumns, lowColumnsContiguous:lowColumns.at(-1)-lowColumns[0]+1===lowColumns.length,
        holes:holeCells.length, holeRows:new Set(holeCells.map(h=>h.y)).size,
        shallowHoles:holeCells.filter(h=>h.cover<=2).length,
        deepHoles:holeCells.filter(h=>h.cover>=3).length,
        holeCover:holeCells.reduce((a,h)=>a+h.cover,0),
        maxHoleCover:Math.max(0,...holeCells.map(h=>h.cover)),
        sidewaysReachableHoles:holeCells.filter(h=>reachable.has(h.y*W+h.x)).length,
        sealedHoles:holeCells.filter(h=>!reachable.has(h.y*W+h.x)).length,
        surfaceHoles:surfaceHoles.length,
        surfaceMaxCover:Math.max(0,...surfaceHoles.map(h=>h.cover)),
        surfaceShallowHoles:surfaceHoles.filter(h=>h.cover<=2).length,
        surfaceSealedHoles:surfaceHoles.filter(h=>!reachable.has(h.y*W+h.x)).length,
        garbageRows, garbageChannelCells:garbageChannels.length,
        garbageChannelCover:garbageChannels.reduce((a,h)=>a+h.cover,0),
        holeCells, garbageChannels,
    };
}

function snapshot(state) {
    const rows=state.board.map(row=>row.map(c=>c===null?'.':/^[iotlszj]$/i.test(c)?c:'#').join(''));
    const s=state.stats;
    const rawNext=[state.falling.type,...state.bag],previewCount=state.setoptions.nextcount??5;
    return {rows,terrain:terrain(rows),hold:state.hold,
        next:rawNext.slice(0,7), // Legacy raw observation; not a visible queue.
        visible_next:rawNext.slice(0,previewCount+1),preview_count:previewCount,
        pending:state.impendingdamage.reduce((a,b)=>a+b.amt,0),
        stats:{piecesplaced:s.piecesplaced,lines:s.lines,btb:s.btb,topbtb:s.topbtb,combo:s.combo,
            topcombo:s.topcombo,tspins:s.tspins,clears:{...s.clears},garbage:{...s.garbage}}};
}

function extract(reference,filename,maxRounds=Infinity,includeEntry=()=>true) {
    const data=JSON.parse(fs.readFileSync(filename,'utf8'));
    const rounds=[];
    for(const [ri,entries] of data.replay.rounds.entries()) {
        if(ri>=maxRounds)break;
        for(const [si,entry] of entries.entries()) {
            if(!includeEntry({round:ri+1,side:si,entry}))continue;
            const replay=new reference.Replay(entry.replay);
            const game=reference.create('replay',{replay});
            game.setHeadless(true);game.setGame(entry.replay.options);game.startGame();
            const placements=[];
            let pending=null;
            const finish=()=> {
                if(!pending)return;
                const after=snapshot(game.state), before=pending.before;
                if(after.stats.piecesplaced!==before.stats.piecesplaced+1)
                    throw Error('Lock/piece boundary did not enclose one placement');
                pending.after=after;
                pending.delta={lines:after.stats.lines-before.stats.lines,
                    attack:after.stats.garbage.attack-before.stats.garbage.attack,
                    sent:after.stats.garbage.sent-before.stats.garbage.sent,
                    received:after.stats.garbage.received-before.stats.garbage.received,
                    garbageCleared:after.stats.garbage.cleared-before.stats.garbage.cleared,
                    garbageRowsAdded:Math.max(0,after.terrain.garbageRows-before.terrain.garbageRows),
                    clears:Object.fromEntries(Object.keys(after.stats.clears).map(k=>
                        [k,after.stats.clears[k]-(before.stats.clears[k]||0)]).filter(x=>x[1]))};
                pending.spin=Object.keys(pending.delta.clears).some(k=>k.includes('spin'));
                placements.push(pending);pending=null;
            };
            game.on('lock',(piece)=> {
                if(pending)throw Error('Previous lock was not finalized');
                pending={index:game.state.stats.piecesplaced+1,frame:game.getFrame(),
                    piece:{type:piece.type,x:piece.x,y:piece.y,r:piece.r,kick:piece.kick,flags:piece.flags},
                    before:snapshot(game.state)};
            });
            game.on('piece',finish);
            for(let f=0;f<=replay.frame;++f) {game.doFrame();finish();}
            const final=snapshot(game.state).stats;
            const mismatches=[];
            // Winner end events may leave finaltime unset in the headless state;
            // finaltime is presentation elapsed time, not a cumulative gameplay stat.
            const checkedStats=Object.keys(entry.replay.results.stats).filter(k=>k!=='finaltime');
            for(const key of checkedStats) {
                if(JSON.stringify(game.state.stats[key])!==JSON.stringify(entry.replay.results.stats[key]))
                    mismatches.push({key,actual:game.state.stats[key],expected:entry.replay.results.stats[key]});
            }
            if(mismatches.length)throw Error(`${path.basename(filename)} round ${ri+1} ${entry.username} stats mismatch: ${JSON.stringify(mismatches)}`);
            if(placements.length!==final.piecesplaced)throw Error('Placement count mismatch');
            rounds.push({file:path.basename(filename),round:ri+1,side:si,username:entry.username,
                result:entry.replay.results.gameoverreason,options:entry.replay.options,final,
                validation:'official engine cumulative end stats matched (finaltime excluded)',checkedStats,placements});
            process.stdout.write(`${path.basename(filename)} round ${ri+1} ${entry.username}: ${placements.length} locks verified\n`);
        }
    }
    return rounds;
}

function quantiles(values) {
    values=values.toSorted((a,b)=>a-b);
    return {mean:values.reduce((a,b)=>a+b,0)/Math.max(1,values.length),
        p50:values[Math.floor((values.length-1)*.5)]??0,
        p90:values[Math.floor((values.length-1)*.9)]??0,
        p99:values[Math.floor((values.length-1)*.99)]??0};
}

function summarize(rounds) {
    const groups={};
    for(const r of rounds) {
        const key=r.file+':'+r.username;
        (groups[key]??=[]).push(r);
    }
    return Object.entries(groups).map(([group,rs])=> {
        const ps=rs.flatMap(r=>r.placements), clears=ps.filter(p=>p.delta.lines), spins=ps.filter(p=>p.spin),
            tSpins=spins.filter(p=>p.piece.type==='t'), nonTSpins=spins.filter(p=>p.piece.type!=='t'),
            normalClears=clears.filter(p=>!p.spin&&p.delta.lines<4), preparations=[];
        for(const r of rs)for(let i=0;i<r.placements.length;++i) {
            const p=r.placements[i];
            if(p.delta.lines||p.after.terrain.holes<=p.before.terrain.holes)continue;
            const later=r.placements.slice(i+1,i+4);
            const spin=later.find(p=>p.spin&&p.delta.lines>0);
            if(spin)preparations.push({round:r.round,index:p.index,spinIndex:spin.index,
                addedHoles:p.after.terrain.holes-p.before.terrain.holes,
                cover:p.after.terrain.maxHoleCover,spinType:spin.piece.type});
        }
        const metrics={};
        for(const key of ['maxHeight','meanHeight','bumpiness','maxWell','holes','holeRows','shallowHoles',
            'deepHoles','holeCover','maxHoleCover','sidewaysReachableHoles','sealedHoles',
            'garbageRows','garbageChannelCover','surfaceHoles','surfaceMaxCover','surfaceShallowHoles',
            'surfaceSealedHoles','deepWellCount'])metrics[key]=quantiles(ps.map(p=>p.after.terrain[key]));
        return {group,rounds:rs.length,wins:rs.filter(r=>r.result==='winner').length,pieces:ps.length,
            lines:ps.reduce((a,p)=>a+p.delta.lines,0),attack:ps.reduce((a,p)=>a+p.delta.attack,0),
            sent:ps.reduce((a,p)=>a+p.delta.sent,0),attackPerPiece:ps.reduce((a,p)=>a+p.delta.attack,0)/ps.length,
            maxB2B:Math.max(...ps.map(p=>p.after.stats.btb)),clearPlacements:clears.length,
            spinPlacements:spins.length,tSpinPlacements:tSpins.length,nonTSpinPlacements:nonTSpins.length,
            spinsByPiece:Object.fromEntries('iotlszj'.split('').map(t=>[t,spins.filter(p=>p.piece.type===t).length])),
            spinSingles:spins.filter(p=>p.delta.lines===1).length,spinDoubles:spins.filter(p=>p.delta.lines===2).length,
            quads:clears.filter(p=>p.delta.lines===4).length,normalClears:normalClears.length,
            b2bBreaks:ps.filter(p=>p.before.stats.btb>0&&p.after.stats.btb===0).length,
            holesStates:ps.filter(p=>p.after.terrain.holes>0).length,
            shallowOnlyHoleStates:ps.filter(p=>p.after.terrain.holes>0&&p.after.terrain.maxHoleCover<=2).length,
            holeAddingNonclearPreparations:preparations.length,preparations,metrics};
    });
}

function summarizeStructures(rounds) {
    const result=[];
    for(const username of new Set(rounds.map(r=>r.username))) {
        const rs=rounds.filter(r=>r.username===username),all=rs.flatMap(r=>r.placements);
        const cohorts={all,
            beforeNonTSpinClear:all.filter(p=>p.spin&&p.piece.type!=='t'&&p.delta.lines),
            successiveNonTSpin:rs.flatMap(r=>r.placements.filter((p,i,ps)=>
                p.spin&&p.piece.type!=='t'&&p.delta.lines&&
                ps.slice(Math.max(0,i-3),i).some(q=>q.spin&&q.piece.type!=='t'&&q.delta.lines)))};
        for(const [cohort,ps] of Object.entries(cohorts)) {
            const ts=ps.map(p=>p.before.terrain),count=f=>ts.filter(f).length;
            const mean=k=>ts.reduce((a,t)=>a+t[k],0)/Math.max(1,ts.length);
            result.push({username,cohort,states:ts.length,
                localDeepWells:{zero:count(t=>t.deepWellCount===0),one:count(t=>t.deepWellCount===1),
                    multiple:count(t=>t.deepWellCount>=2)},
                oneLowColumn:count(t=>t.lowColumns.length===1),
                twoOrThreeContiguousLowColumns:count(t=>t.lowColumns.length>=2&&t.lowColumns.length<=3&&t.lowColumnsContiguous),
                surfaceHoles:mean('surfaceHoles'),surfaceShallowHoles:mean('surfaceShallowHoles'),
                surfaceMaxCover:mean('surfaceMaxCover'),surfaceSealedHoles:mean('surfaceSealedHoles'),
                surfaceHoleStates:count(t=>t.surfaceHoles>0),
                allSurfaceHolesCoveredByAtMostTwo:count(t=>t.surfaceHoles>0&&t.surfaceMaxCover<=2)});
        }
    }
    return result;
}

function ascii(rows,maxRows=16) {
    const top=rows.findIndex(r=>/[^.]/.test(r));
    return rows.slice(Math.max(0,top-1,rows.length-maxRows)).map((r,i,a)=>`${String(a.length-i).padStart(2)} |${r}|`).join('\n');
}

function examples(rounds,count=5) {
    const candidates=[];
    for(const r of rounds.filter(r=>r.username.includes('moch'))) {
        const ps=r.placements;
        for(let i=1;i<ps.length-4;++i) {
            const first=ps[i];
            if(first.delta.lines||first.after.terrain.holes<=first.before.terrain.holes)continue;
            for(let j=i+1;j<=Math.min(i+4,ps.length-1);++j) {
                const spin=ps[j];
                if(!spin.spin||!spin.delta.lines)continue;
                const tail=ps.slice(j+1,j+7).find(p=>p.spin&&p.delta.lines);
                if(!tail)continue;
                const window=ps.slice(i,ps.indexOf(tail)+1);
                if(window.some(p=>p.delta.garbageRowsAdded||p.delta.lines&&!p.spin&&p.delta.lines<4)||first.after.terrain.maxHoleCover>3)continue;
                if(first.before.terrain.maxHeight>13)continue;
                candidates.push({file:r.file,round:r.round,start:first.index,end:tail.index,
                    score:window.filter(p=>p.spin&&p.delta.lines).length*10-window.length,placements:window});
                break;
            }
        }
    }
    // Stable reviewed cases make reports comparable between extraction runs.
    // They include both broad work areas and roofs removed on the next clear.
    const selected=[];
    for(const [round,start,end] of [[4,266,269],[2,26,28],[6,174,176],[3,159,161],[1,78,80]]) {
        const r=rounds.find(r=>r.file==='mochengine2vszzz.ttrm'&&r.round===round&&r.username==='mochbot-dev');
        if(!r)continue;
        const placements=r.placements.filter(p=>p.index>=start&&p.index<=end);
        if(placements.length!==end-start+1||placements.some(p=>p.delta.garbageRowsAdded))
            throw Error('Reviewed example no longer matches its placement interval');
        selected.push({file:r.file,round,start,end,selection:'reviewed representative case',placements});
        if(selected.length>=count)return selected;
    }
    for(const candidate of candidates.toSorted((a,b)=>b.score-a.score)) {
        if(selected.some(c=>c.file===candidate.file&&c.round===candidate.round&&Math.abs(c.start-candidate.start)<20))continue;
        selected.push(candidate);if(selected.length>=count)break;
    }
    return selected;
}

function report(summary,selected,structures) {
    const lines=['# Mochbot terrain observations','',
        'These are observations, not a reconstruction of the private bot algorithm. Every recorded cumulative end-stat field was compared with the official headless engine; finaltime is excluded because winner end events can leave it unset. Boards are top to bottom; dots are empty, `#` is garbage, letters retain piece colors. Hole cover is the number of occupied cells above that hole in its column. “Sideways reachable” uses a point flood fill, not tetromino reachability. Raw after-board snapshots include garbage received at that placement.','',
        '**Timing and visibility:** `before` is captured by the lock event, after any HOLD and movement. Its HOLD/current state cannot train the preceding HOLD decision. Legacy `next` is current plus up to six raw bag entries; the audited replays have official nextcount=5, so training/search must use only its first six entries (current + five previews). The seventh raw entry is not visible information. Future exports additionally expose `visible_next` and `preview_count`. Existing frozen JSON is not rewritten. All historical NEXT tables below are raw observations, not a policy input contract.','',
        '| Match/player | Pieces | Attack/piece | Spin/T/non-T | Spin 1/2 lines | Quads | Normal 1–3 clears | B2B breaks | Max B2B |',
        '|---|---:|---:|---:|---:|---:|---:|---:|---:|'];
    for(const s of summary)lines.push(`| ${s.group} | ${s.pieces} | ${s.attackPerPiece.toFixed(3)} | ${s.spinPlacements}/${s.tSpinPlacements}/${s.nonTSpinPlacements} | ${s.spinSingles}/${s.spinDoubles} | ${s.quads} | ${s.normalClears} | ${s.b2bBreaks} | ${s.maxB2B} |`);
    lines.push('','| Match/player | Height mean/p90 | Holes mean/p90 | Cover mean/p90 | States with holes | Hole states covered by ≤2 | Nonclear adds holes, spin within 3 |','|---|---:|---:|---:|---:|---:|---:|');
    for(const s of summary) {
        const fmt=k=>`${s.metrics[k].mean.toFixed(2)}/${s.metrics[k].p90}`;
        lines.push(`| ${s.group} | ${fmt('maxHeight')} | ${fmt('holes')} | ${fmt('maxHoleCover')} | ${s.holesStates} | ${s.shallowOnlyHoleStates} | ${s.holeAddingNonclearPreparations} |`);
    }
    lines.push('','## Surface structure, excluding rows that contain garbage','',
        'The table below is restricted to the nine-round vs zzz replay. A surface hole is a covered empty cell in a row with no garbage cells. A local deep well has min(neighbor heights) minus its height ≥3; the side wall counts as height 40. Low columns are within 2 rows of the global minimum height. A successive non-T spin is a non-T spin clear preceded by another non-T spin clear within 3 placements. These are descriptive definitions, not proofs of move reachability or causal quality.','',
        '| Player/cohort | States | Local deep wells: 0/1/2+ | Surface holes mean | Shallow (cover ≤2) mean | Surface sealed mean | All surface holes shallow / with any |',
        '|---|---:|---:|---:|---:|---:|---:|');
    for(const s of structures)lines.push(`| ${s.username}/${s.cohort} | ${s.states} | ${s.localDeepWells.zero}/${s.localDeepWells.one}/${s.localDeepWells.multiple} | ${s.surfaceHoles.toFixed(3)} | ${s.surfaceShallowHoles.toFixed(3)} | ${s.surfaceSealedHoles.toFixed(3)} | ${s.allSurfaceHolesCoveredByAtMostTwo}/${s.surfaceHoleStates} |`);
    lines.push('','Interpretation: aggregate hole depth confounds garbage shafts and constructed overhangs. In the vs zzz sample, mochbot’s surface holes are predominantly covered by one or two cells, while it avoids simultaneous multiple local deep wells. Most successive non-T spin clears have no local deep well at all. This supports distinguishing shallow workable overhangs from deeply buried cavities; it does not support requiring a deep one-column well.','',
        'The round 4 pieces 266–268 example is a counterexample to a compulsory single-well model and to treating every geometrically sealed hole as expensive: I bridges the center, L clears that bridge row from the right, and Z then clears from the center. The covered layer is only one empty cell away from a line clear. A roof’s clearability and visible piece supply matter beyond flood-fill connectivity. This is an evaluator hypothesis suggested by the replay, not a claim about mochbot’s implementation.','',
        'Round 2 pieces 26–28 illustrates two separated work areas: T clears the lower left, then Z clears the lower right. Penalizing every second depression independently of available placements can disfavor this observed sequence.','',
        '## Representative observed sequences','',
        'These sequences add covered cells without clearing, then perform multiple difficult clears (spins or quads). They have no newly received garbage during the illustrated interval. Selection does not establish the bot’s intent or causal strength.');
    for(const c of selected) {
        lines.push('',`### ${c.file}, round ${c.round}, pieces ${c.start}–${c.end}`,'',
            'Before:','```',ascii(c.placements[0].before.rows),'```','',
            '| Piece | Placement | Clear | Attack | B2B | Holes | Max cover | Hold after | Next after | Heights |','|---:|---|---|---:|---:|---:|---:|---|---|---|');
        for(const p of c.placements)lines.push(`| ${p.index} | ${p.piece.type.toUpperCase()} x=${p.piece.x} r=${p.piece.r} | ${Object.keys(p.delta.clears).join(', ')||'none'} | ${p.delta.attack} | ${p.after.stats.btb} | ${p.after.terrain.holes} | ${p.after.terrain.maxHoleCover} | ${p.after.hold??'-'} | ${p.after.next.join(' ')} | ${p.after.terrain.heights.join(',')} |`);
        for(const p of c.placements)lines.push('',`After piece ${p.index}:`,'```',ascii(p.after.rows),'```');
    }
    return lines.join('\n')+'\n';
}

if(require.main===module) {
    const [bundle,outdir,...specs]=process.argv.slice(2);
    if(!bundle||!outdir||!specs.length)throw Error('Usage: node moch_terrain.cjs <reviewed bundle> <outdir> <replay.ttrm[:maxRounds]> ...');
    fs.mkdirSync(outdir,{recursive:true});
    const reference=loadReference(bundle),rounds=[];
    for(const spec of specs) {
        const match=spec.match(/^(.*\.ttrm)(?::(\d+))?$/);
        if(!match)throw Error('Invalid replay specification: '+spec);
        const extracted=extract(reference,match[1],match[2]?Number(match[2]):Infinity);
        rounds.push(...extracted);
        fs.writeFileSync(path.join(outdir,path.basename(match[1],'.ttrm')+'.placements.json'),JSON.stringify({reviewedSha256,rounds:extracted})+'\n');
    }
    const summary=summarize(rounds),selected=examples(rounds),
        structures=summarizeStructures(rounds.filter(r=>r.file==='mochengine2vszzz.ttrm'));
    fs.writeFileSync(path.join(outdir,'summary.json'),JSON.stringify({reviewedSha256,summary},null,2)+'\n');
    fs.writeFileSync(path.join(outdir,'examples.json'),JSON.stringify(selected,null,2)+'\n');
    fs.writeFileSync(path.join(outdir,'structures.json'),JSON.stringify(structures,null,2)+'\n');
    fs.writeFileSync(path.join(outdir,'observations.md'),report(summary,selected,structures));
}

module.exports={terrain,snapshot,extract,summarize,summarizeStructures,examples,report};
