#!/usr/bin/env python3
"""Fixed 24-root diagnosis; no training, candidate generation, or BOT writes."""
import hashlib
import json
from pathlib import Path
import sys
from moch_pattern_imitation import pattern_counts

BASE = Path('runlogs/chatgpt_moch_20260926')
OUT = BASE / 'fullsearch_teacher'
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def write(name, value): (OUT / name).write_text(json.dumps(value, indent=2) + '\n')
def stable_id(s): return f"{s['source']}:{s['round']}:{s['placement']}:mochbot-dev"
def prepare():
    OUT.mkdir(exist_ok=False)
    write('preregistration.json', {'scope':'development only, existing pre-native eligible roots; retain coverage failures',
        'selection':'8 development opponent groups, 3 smallest SHA256 UTF8(source:round:placement:mochbot-dev) per group',
        'roots':24, 'repeats':2, 'order':['baseline,pattern_v1','pattern_v1,baseline'], 'milliseconds':100,
        'state':'post-HOLD current family fixed with hold_free=false; real future HOLD; current+5 only; host spawn; no pressure added',
        'one_step':'existing production_present candidates; base=fixed_reward+old_terrain; pattern adds frozen raw144 table; strict better scores define tied rank',
        'full_search':'cold root; host reachable first move; exact family+4cells+spin+mini+lines+post_rows teacher match; geometry separately',
        'metrics':['all24 coverage and failures','one_step teacher rank','full_search teacher exact hit','geometry-only hit','evals/gets/depth histogram','paired first-move changes'],
        'limits':'No model tuning, selection by ranks, external data, extra roots or live victory claims',
        'model_sha256':sha(BASE/'pattern_pilot_fit/model.json'), 'manifest_sha256':sha(BASE/'pattern_pilot/manifest.json')})
    manifest=json.loads((BASE/'pattern_pilot/manifest.json').read_text())
    groups=sorted({s['opponent_group'] for s in manifest['samples'] if s['split']=='development'})
    assert len(groups)==8
    selected=[s for g in groups for s in sorted((s for s in manifest['samples'] if s['split']=='development' and s['opponent_group']==g),key=lambda s:hashlib.sha256(stable_id(s).encode()).hexdigest())[:3]]
    chosen={s['sample_id']:s for s in selected}; write('selection.json',selected)
    before={}
    for export in manifest['source_exports']:
        data=json.loads(Path(export['path']).read_text())
        for r in data['rounds']:
            if r['username']!='mochbot-dev': continue
            for s in selected:
                if (s['source'],s['round'])==(r['file'],r['round']):
                    p=next(p for p in r['placements'] if p['index']==s['placement']); before[s['sample_id']]=p['before']
    roots=[]; labels={}
    for line in (BASE/'pattern_pilot/roots.log').read_text().splitlines():
        fields=dict(t.split('=',1) for t in line.split()[1:]); sid=int(fields['sample_id'])
        if sid not in chosen: continue
        s=chosen[sid]; b=before[sid]; visible=s['post_hold_visible_current_and_next']; assert visible==b['next'][:6]
        fields['root_hold']=str('ijlostz'.index(b['hold'])) if b['hold'] else '-1'
        fields['root_next']=','.join(str('ijlostz'.index(c)) for c in visible)
        roots.append((sid,'TETR_BOT_DIAGNOSTIC '+' '.join(f'{k}={v}' for k,v in fields.items())))
        labels[sid]={'family':'ijlostz'.index(visible[0]),'cells':sorted([list(map(int,c.split(':'))) for c in fields['teacher_cells'].split(',')]),
            'post_rows':list(map(int,fields['teacher_after'].split(','))),**{k:int(fields['teacher_'+k]) for k in ('lines','spin','mini')}}
    assert len(roots)==24 and len(before)==24
    (OUT/'roots.log').write_text('\n'.join(dict(roots)[s['sample_id']] for s in selected)+'\n');write('labels.json',labels)
    table=json.loads((BASE/'pattern_pilot_fit/model.json').read_text())['table_scores']; rankings=[]
    with (BASE/'pattern_pilot/candidates.jsonl').open() as f:
        for line in f:
            r=json.loads(line); sid=r['sample_id']
            if sid not in chosen: continue
            candidates=[c for c in r['candidates'] if c['production_present']]; positive=[i for i,c in enumerate(candidates) if c['positive']]
            item={'sample_id':sid,'production_candidates':len(candidates),'native_teacher_match_count':r['teacher_match_count'],'production_teacher_match_count':r['production_teacher_match_count'],'reject_reason':r['reject_reason']}
            base=[c['fixed_reward']+c['old_terrain'] for c in candidates]
            for name,scores in [('baseline',base),('pattern_v1',[v+sum(w*n for w,n in zip(table,pattern_counts(c['post_rows']))) for v,c in zip(base,candidates)])]:
                item[name+'_rank']=1+sum(v>max(scores[i] for i in positive)+1e-9 for v in scores) if positive else None
            rankings.append(item)
    write('one_step.json',rankings)
    write('input_provenance.json',{'roots_sha256':sha(OUT/'roots.log'),'selection_sha256':sha(OUT/'selection.json'),'existing_candidates_sha256':sha(BASE/'pattern_pilot/candidates.jsonl')})

def summarize():
    labels=json.loads((OUT/'labels.json').read_text()); records=[json.loads(l) for l in (OUT/'results.jsonl').read_text().splitlines()]; assert len(records)==96
    samples={s['sample_id']:s for s in json.loads((OUT/'selection.json').read_text())}; ranks={r['sample_id']:r for r in json.loads((OUT/'one_step.json').read_text())}
    for r in records:
        t=labels[str(r['sample_id'])]; r['opponent_group']=samples[r['sample_id']]['opponent_group']
        r['geometry_match']=not r['failed'] and r['family']==t['family'] and sorted(r['cells'])==t['cells']
        r['teacher_match']=r['geometry_match'] and bool(r['legal']) and all(r[k]==t[k] for k in ('spin','mini','lines','post_rows'))
        r['one_step_rank']=ranks[r['sample_id']][r['variant']+'_rank']
    write('annotated.json',records)
    summary={}
    for variant in ('baseline','pattern_v1'):
        rr=[r for r in records if r['variant']==variant]; rank=[r[variant+'_rank'] for r in ranks.values()]
        summary[variant]={'one_step_top1':sum(n==1 for n in rank),'one_step_available':sum(n is not None for n in rank),'full_teacher_hits':sum(r['teacher_match'] for r in rr),'geometry_hits':sum(r['geometry_match'] for r in rr),'runs':len(rr),'failures':sum(r['failed'] for r in rr),'illegal':sum(not r.get('legal',False) for r in rr),'mean_evals':sum(r.get('evals',0) for r in rr)/len(rr),'one_step_top1_but_full_miss':sum(r['one_step_rank']==1 and not r['teacher_match'] for r in rr),'groups':{g:sum(r['teacher_match'] for r in rr if r['opponent_group']==g) for g in sorted({r['opponent_group'] for r in rr})}}
    write('summary.json',summary);print(json.dumps(summary,indent=2))
if __name__=='__main__': {'prepare':prepare,'summarize':summarize}[sys.argv[1]]()
