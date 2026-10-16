#!/usr/bin/env python3
"""Select preregistered tactical case studies from existing verified exports.

No replay execution, BOT search, learning or changes to source artifacts.
"""
from pathlib import Path
from collections import Counter
import hashlib
import json
from moch_pattern_imitation import FILE_GROUPS

BASE = Path('runlogs/chatgpt_moch_20260926')
OUT = BASE / 'tactical_cases'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write(name, data):
    (OUT / name).write_text(json.dumps(data, indent=2) + '\n')

def special(p):
    return p['delta']['lines'] > 0 and (p['spin'] or p['delta']['lines'] == 4)

def runs(placements, predicate):
    start = None
    for i in range(len(placements) + 1):
        yes = i < len(placements) and predicate(placements[i])
        if yes and start is None:
            start = i
        if not yes and start is not None:
            yield start, i
            start = None

def visible(snapshot):
    result = snapshot['next'][:6]
    if len(result) != 6:
        raise ValueError('Missing visible current+5')
    if 'visible_next' in snapshot and snapshot['visible_next'] != result:
        raise ValueError('Explicit/legacy visible queue mismatch')
    return result

def summary(record, start, end, kind):
    ps = record['placements']; segment = ps[start:end]
    previous = ps[max(0, start - 3):start]
    return {'kind': kind, 'source': record['file'], 'round': record['round'],
        'opponent_group': FILE_GROUPS[record['file']], 'start_index': ps[start]['index'],
        'end_index': ps[end - 1]['index'], 'start_offset': start, 'end_offset_exclusive': end,
        'event_placements': end - start, 'start_frame': ps[start]['frame'], 'end_frame': ps[end - 1]['frame'],
        'start_pending_total': ps[start]['before']['pending'],
        'max_pending_total': max(p['before']['pending'] for p in segment),
        'prior3_net_garbage_rows_added': sum(p['delta']['garbageRowsAdded'] for p in previous),
        'garbage_clearing_placements': sum(p['delta']['garbageCleared'] > 0 for p in segment),
        'garbage_cleared': sum(p['delta']['garbageCleared'] for p in segment),
        'attack': sum(p['delta']['attack'] for p in segment),
        'sent': sum(p['delta']['sent'] for p in segment),
        'quads': sum(p['delta']['lines'] == 4 for p in segment),
        'b2b_before': segment[0]['before']['stats']['btb'],
        'b2b_after': segment[-1]['after']['stats']['btb'],
        'max_combo': max(p['after']['stats']['combo'] for p in segment),
        'max_height': max(p['before']['terrain']['maxHeight'] for p in segment),
        'preparation_available': min(6, start), 'aftermath_available': min(3, len(ps) - end)}

def markdown(case, record):
    ps = record['placements']; first = max(0, case['start_offset'] - 6); last = min(len(ps), case['end_offset_exclusive'] + 3)
    lines = [f"# {case['case_id']}: {case['source']} round {case['round']}",
        f"Event placements {case['start_index']}–{case['end_index']}; attack {case['attack']}, sent {case['sent']}, garbage cleared {case['garbage_cleared']}, Quads {case['quads']}, max combo {case['max_combo']}, B2B {case['b2b_before']}→{case['b2b_after']}.",
        'These are selected strong-play examples, not population estimates or BOT match-rate samples. All 40 rows and both snapshots are retained in the adjacent JSON. ASCII rows retain official block colors (# = garbage); y is native bottom-origin. Snapshot time is the official lock frame/60, not wall-clock time. Current+5 is explicitly truncated; the legacy extra raw bag item is never shown as visible. Pending is total queued garbage, not ready garbage.']
    for offset in range(first, last):
        p = ps[offset]; b, a = p['before'], p['after']; d = p['delta']
        role = 'preparation' if offset < case['start_offset'] else 'aftermath' if offset >= case['end_offset_exclusive'] else 'event'
        lines += [f"## p{p['index']} · {role} · {p['piece']['type'].upper()} · frame {p['frame']} ({p['frame']/60:.3f}s)",
            f"Lock x={p['piece']['x']}, y={p['piece']['y']}, r={p['piece']['r']}; clears={d['clears']}; lines={d['lines']}; attack/sent={d['attack']}/{d['sent']}; garbage cleared={d['garbageCleared']}; received counter delta={d['received']}; net added garbage rows={d['garbageRowsAdded']}.",
            f"B2B {b['stats']['btb']}→{a['stats']['btb']}; combo {b['stats']['combo']}→{a['stats']['combo']}; pending total {b['pending']}→{a['pending']}; HOLD {b['hold'] or '-'}→{a['hold'] or '-'}; current+5 {''.join(visible(b))}→{''.join(visible(a))}.",
            '```text', '     before        after']
        top = min(next((i for i,r in enumerate(b['rows']) if r != '.'*10), 39), next((i for i,r in enumerate(a['rows']) if r != '.'*10), 39))
        for y in range(max(0, top-1), 40):
            lines.append(f"{39-y:02}   {b['rows'][y]}    {a['rows'][y]}")
        lines.append('```')
    return '\n\n'.join(lines) + '\n'

def main():
    OUT.mkdir(exist_ok=False)
    criteria = {'scope':'all 106 verified Moch rounds; selected tactical case studies only, not training or matching-rate samples',
        'pressure':'maximal consecutive special-clear placements, each nondecreasing B2B; >=3 garbage-clearing placements and >=6 garbage lines cleared; first before.pending>=12 OR prior3 sum(net garbageRowsAdded)>=8',
        'burst':'maximal consecutive line-clearing combo run; >=2 Quads, >=5 placements and >=24 raw attack; no interruption omitted',
        'context':'retain up to6 preceding placements and up to3 following placements; require full6+3 context for selection',
        'ranking':'pressure: descending garbage_cleared, start_pending_total, attack; burst: descending attack, quads, max_combo; ties source,round,start_index',
        'diversity':'greedy choose at most one per opponent group per kind; at most3 per kind; no outcome filter; no threshold relaxation',
        'limits':'pending lacks ready/FIFO deadline; net added garbage rows is not total delivered/received; all previews current+5; before is after HOLD and movement; no unseed/block parameter is passed to any tool'}
    write('preregistration.json',criteria)
    manifest = json.loads((BASE/'pattern_pilot/manifest.json').read_text())
    records, provenance = {}, []
    for source in manifest['source_exports']:
        path = Path(source['path'])
        if digest(path) != source['sha256']:
            raise ValueError('Frozen export hash mismatch')
        provenance.append(source)
        for r in json.loads(path.read_text())['rounds']:
            if r['username'] != 'mochbot-dev':
                continue
            key = (r['file'],r['round'])
            if key in records or 'matched' not in r['validation']:
                raise ValueError('Duplicate/unverified round')
            records[key] = r
    if len(records) != 106:
        raise ValueError('Expected106 verified rounds')
    candidates = {'pressure':[], 'burst':[]}
    for r in records.values():
        ps = r['placements']
        for start,end in runs(ps, lambda p:special(p) and p['after']['stats']['btb'] >= p['before']['stats']['btb']):
            c = summary(r,start,end,'pressure')
            if c['garbage_clearing_placements']>=3 and c['garbage_cleared']>=6 and (c['start_pending_total']>=12 or c['prior3_net_garbage_rows_added']>=8):
                candidates['pressure'].append(c)
        for start,end in runs(ps, lambda p:p['delta']['lines']>0):
            c = summary(r,start,end,'burst')
            if c['quads']>=2 and c['event_placements']>=5 and c['attack']>=24:
                candidates['burst'].append(c)
    selected = []
    for kind,values in candidates.items():
        key = (lambda c:(-c['garbage_cleared'],-c['start_pending_total'],-c['attack'],c['source'],c['round'],c['start_index'])) if kind=='pressure' else (lambda c:(-c['attack'],-c['quads'],-c['max_combo'],c['source'],c['round'],c['start_index']))
        values.sort(key=key); groups = set()
        for c in values:
            if c['preparation_available']<6 or c['aftermath_available']<3 or c['opponent_group'] in groups:
                continue
            groups.add(c['opponent_group']); c=dict(c,case_id=f'{kind}_{len(groups)}'); selected.append(c)
            r=records[(c['source'],c['round'])]; first=c['start_offset']-6;last=c['end_offset_exclusive']+3
            placements=[]
            for p in r['placements'][first:last]:
                p=json.loads(json.dumps(p))
                for name in ['before','after']:
                    p[name]['visible_current_and_next']=visible(p[name]);p[name]['raw_next_is_not_all_visible']=True
                placements.append(p)
            write(c['case_id']+'.json',{'case':c,'options':r['options'],'validation':r['validation'],'placements':placements})
            (OUT/(c['case_id']+'.md')).write_text(markdown(c,r))
            if len(groups)==3:break
    write('case_manifest.json',{'criteria':criteria,'verified_rounds':len(records),'teacher_placements':sum(len(r['placements']) for r in records.values()),'candidate_counts':{k:len(v) for k,v in candidates.items()},'candidate_opponent_counts':{k:dict(Counter(c['opponent_group'] for c in v)) for k,v in candidates.items()},'selected':selected,'all_candidates':candidates,'source_exports':provenance,'tool_sha256':digest(Path(__file__))})
    print(json.dumps({'candidates':{k:len(v) for k,v in candidates.items()},'selected':selected},indent=2))

if __name__=='__main__':main()
