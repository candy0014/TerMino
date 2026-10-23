// Offline conformance audit. Supply original event clients; none are vendored.
'use strict';
const fs=require('node:fs');
const vm=require('node:vm');
const crypto=require('node:crypto');
const assert=require('node:assert/strict');
const {execFileSync}=require('node:child_process');
const reviewed={
    2025:'6c06cfecae9e7d072cbd75bb2b85bf652a463c25d5163c55b89fde312fa1eaaf',
    2026:'a910b10f51e8780e0b4856ab21c2fd5d3cb71d151e173f220d6ea8f7cc1efa5c',
};
function braces(source,start) {
    assert.equal(source[start],'{');let depth=0,quote='',escape=false;
    for(let i=start;i<source.length;++i) {
        const c=source[i];
        if(quote) {if(escape)escape=false;else if(c==='\\')escape=true;else if(c===quote)quote='';}
        else if(c==='"'||c==="'"||c==='`')quote=c;
        else if(c==='{')++depth;
        else if(c==='}'&&!--depth)return source.slice(start,i+1);
    }
    throw Error('Unterminated source object/method');
}
function load(filename,year) {
    const bytes=fs.readFileSync(filename);
    assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),reviewed[year],'Unreviewed event client');
    const source=bytes.toString('utf8'),base=source.indexOf('const z={minotypes:');assert(base>=0);
    const object=name=>{
        const start=source.indexOf(name+':{',base)+name.length+1;assert(start>base);
        return vm.runInNewContext('('+braces(source,start)+')',{}, {timeout:1000});
    };
    const method=marker=>{
        const start=source.indexOf(marker,base);assert(start>base);
        const body=source.indexOf('{',start);
        // Execute the unchanged original method in a small, offline harness.
        return vm.runInNewContext('({'+source.slice(start,body)+braces(source,body)+'})',{
            W:{ROTATION_ALL:63,STATE_ALL:960,ACTION_FORCELOCK:2048,ACTION_SOFTDROP:4096,
                ACTION_MOVE:8192,ACTION_ROTATE:16384,ACTION_IHS:1024},
        },{timeout:1000});
    };
    return {tetrominoes:object('tetrominoes'),kicksets:object('kicksets'),
        ...method('GetPieceOffset(e){'),...method('GetPieceSpawnRotation(e){'),
        ...method('GetPieceDefinition(e,t,n=!1){'),...method('Next(e,t=!1,n=!1){')};
}
if(process.argv.length!==5 && process.argv.length!==6) {
    console.error('Usage: node tools/audit_tetrio_event_geometry.cjs <2025-event.js> <2026-event.js> <fixture-program> [test-case]');
    process.exit(2);
}
const clients={2025:load(process.argv[2],2025),2026:load(process.argv[3],2026)};
const fixtures=JSON.parse(execFileSync(process.argv[4],[...process.argv.slice(5),'--fixtures'],{encoding:'utf8',maxBuffer:4*1024*1024}));
const sort=cells=>Array.from(cells,cell=>Array.from(cell)).sort((a,b)=>a[0]-b[0]||a[1]-b[1]);
const transitions=['01','10','12','21','23','32','30','03','02','20','13','31'];
const counts={pieces:0,matrices:0,spawns:0,kicks:0};
for(const f of fixtures) {
    const year=f.system===6?2026:2025,c=clients[year];
    let name=f.name.normalize('NFKC').toLowerCase();
    if(year===2026)name=name[0]+'-'+name.slice(1);
    if(name==='farmalhaut')name='formalhaut';
    const kickset=c.kicksets[year===2026?'SRS+':'ASCDX'];
    const cm={kickset,constants:{tetrominoes:c.tetrominoes},
        GetPieceOffset:c.GetPieceOffset,GetPieceSpawnRotation:c.GetPieceSpawnRotation};
    const piece={skip:[],flags:0,irs:0};
    const harness={W:10,B:20,piece,options:{version:19},
        S:{handling:{irs:'off',ihs:'off'},zenith:{},stats:{combo:1}},
        self:{cm,bm:{ClearIsLegalAtPosMemo(){}},hm:{H:{}},iom:{IsLocal(){return false;}},svm:{Update(){}}},
        _InternalDCD(){},ConsiderBlockout(){return false;},Is20G(){return false;}};
    const definitionHarness={piece,self:{cm}};
    for(let r=0;r<4;++r) {
        const m=c.GetPieceDefinition.call(definitionHarness,name,r);
        const actual=m.data.map(([x,y])=>[y-m.dy,x-m.dx]);
        assert.deepEqual(sort(f.shapes[r]),sort(actual),`${year} ${name} rotation ${r} matrix`);++counts.matrices;
    }
    for(const [width,row,col,rotation] of f.spawns) {
        harness.W=width;c.Next.call(harness,name);
        assert.deepEqual([row,col,rotation],[Math.ceil(piece.y)-harness.B,piece.x,piece.r],`${year} ${name} width ${width} spawn`);
        ++counts.spawns;
    }
    const definition=c.tetrominoes[name];
    const kicks=kickset[`${definition.kickset_special}_kicks`]??kickset.kicks;
    transitions.forEach((key,index)=>{
        const expected=[[0,0],...(kicks[key]??[]).map(([x,y])=>[x,-y||0])];
        assert.deepEqual(f.kicks[index],expected,`${year} ${name} ${key} kick order`);++counts.kicks;
    });
    ++counts.pieces;
}
assert.equal(counts.pieces,148);
console.log(JSON.stringify({officialClients:reviewed,...counts,passed:true},null,2));
