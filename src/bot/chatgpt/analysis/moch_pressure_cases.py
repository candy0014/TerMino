#!/usr/bin/env python3
"""Coordinator-requested pressure-boundary correction, preserving v1 artifacts."""
from pathlib import Path
import json
import shutil
from collections import Counter
import moch_tactical_cases as C

OLD=C.BASE/'tactical_cases';OUT=C.BASE/'tactical_cases_v2'
def write(name,obj): (OUT/name).write_text(json.dumps(obj,indent=2)+'\n')
def zero_gap(ps):
    longest=current=0
    for p in ps:
        current=current+1 if not p['delta']['lines'] else 0
        longest=max(longest,current)
    return longest
def main():
    OUT.mkdir(exist_ok=False)
    old=json.loads((OLD/'case_manifest.json').read_text())
    plan={'amendment_reason':'Coordinator clarified B2B-preserving pressure episodes must allow zero-line placements; original consecutive-clear v1 remains unchanged',
        'pressure_boundary':'maximal contiguous run of zero-line placements or clears that strictly increment B2B; ends at an ordinary clear that resets/leaves B2B0; includes all intervening and trailing zero-line placements',
        'pressure_gate':'first point in the run with before.B2B>0 and (before.pending>=12 OR previous3 net garbageRowsAdded>=8), followed within the run by >=3 garbage-clearing placements and >=6 garbage rows cleared',
        'ranking':'descending post-trigger garbage cleared, trigger pending, full-run attack; then source,round,start_index',
        'context':'full maximal run plus exactly6 preceding and3 following placements; exclude incomplete context, at most1 per opponent and at most3; record longest zero-line gap',
        'burst':'reuse all3 v1 frozen selections without rescanning or changes',
        'limits':'pending is not ready; net added garbage rows is not received; current+5 only; case study, not population/fit metrics'}
    write('preregistration.json',plan)
    records={}
    for source in old['source_exports']:
        path=Path(source['path']);assert C.digest(path)==source['sha256']
        for r in json.loads(path.read_text())['rounds']:
            if r['username']=='mochbot-dev':records[(r['file'],r['round'])]=r
    assert len(records)==106
    candidates=[]
    for r in records.values():
        ps=r['placements']
        for start,end in C.runs(ps,lambda p:p['delta']['lines']==0 or p['after']['stats']['btb']>p['before']['stats']['btb']):
            for trigger in range(start,end):
                p=ps[trigger];prior=sum(q['delta']['garbageRowsAdded'] for q in ps[max(0,trigger-3):trigger]);tail=ps[trigger:end]
                if p['before']['stats']['btb']<=0 or (p['before']['pending']<12 and prior<8):continue
                gc=sum(q['delta']['garbageCleared'] for q in tail);n=sum(q['delta']['garbageCleared']>0 for q in tail)
                if n<3 or gc<6:continue
                c=C.summary(r,start,end,'pressure');c.update(pressure_trigger_index=p['index'],pressure_trigger_frame=p['frame'],trigger_pending_total=p['before']['pending'],trigger_prior3_net_garbage_rows_added=prior,trigger_b2b=p['before']['stats']['btb'],post_trigger_garbage_cleared=gc,post_trigger_garbage_clearing_placements=n,longest_zero_line_gap=zero_gap(ps[start:end]))
                candidates.append(c);break
    candidates.sort(key=lambda c:(-c['post_trigger_garbage_cleared'],-c['trigger_pending_total'],-c['attack'],c['source'],c['round'],c['start_index']))
    selected=[];groups=set()
    for c in candidates:
        if c['preparation_available']<6 or c['aftermath_available']<3 or c['opponent_group'] in groups:continue
        groups.add(c['opponent_group']);c=dict(c,case_id=f'pressure_{len(groups)}');selected.append(c);r=records[(c['source'],c['round'])]
        placements=[]
        for p in r['placements'][c['start_offset']-6:c['end_offset_exclusive']+3]:
            p=json.loads(json.dumps(p))
            for name in ['before','after']:p[name]['visible_current_and_next']=C.visible(p[name]);p[name]['raw_next_is_not_all_visible']=True
            placements.append(p)
        write(c['case_id']+'.json',{'case':c,'options':r['options'],'validation':r['validation'],'placements':placements})
        text=C.markdown(c,r);text+=f"\nPressure trigger: p{c['pressure_trigger_index']}, pending total {c['trigger_pending_total']}, prior3 net added garbage rows {c['trigger_prior3_net_garbage_rows_added']}, B2B {c['trigger_b2b']}. From trigger to end: {c['post_trigger_garbage_cleared']} garbage rows cleared. Longest no-clear gap within the complete run: {c['longest_zero_line_gap']} placements.\n"
        (OUT/(c['case_id']+'.md')).write_text(text)
        if len(groups)==3:break
    for c in old['selected']:
        if c['kind']!='burst':continue
        selected.append(c)
        for ext in ['json','md']:shutil.copyfile(OLD/(c['case_id']+'.'+ext),OUT/(c['case_id']+'.'+ext))
    write('case_manifest.json',{'criteria':plan,'verified_rounds':106,'teacher_placements':13784,'pressure_candidate_count':len(candidates),'pressure_candidate_groups':dict(Counter(c['opponent_group'] for c in candidates)),'selected':selected,'all_pressure_candidates':candidates,'source_exports':old['source_exports'],'tool_sha256':C.digest(Path(__file__)),'original_burst_manifest_sha256':C.digest(OLD/'case_manifest.json')})
    print(json.dumps({'pressure_candidates':len(candidates),'selected':selected},indent=2))
if __name__=='__main__':main()
