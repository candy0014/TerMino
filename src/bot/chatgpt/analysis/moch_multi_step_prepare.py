#!/usr/bin/env python3
"""Keep every frozen root as a proposed five-step window, including exclusions."""
import argparse
import hashlib
import json
from pathlib import Path

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--input',type=Path,default=Path('runlogs/chatgpt_moch_20260926/tactical_cases_v2/pressure_inputs'))
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args();prepared=json.loads((a.input/'prepared.json').read_text())['records']
    by_key={(r['case_id'],r['placement']):r for r in prepared};entries=[];lines=[]
    for r in prepared:
        window=[]
        for i in range(5):
            item=by_key.get((r['case_id'],r['placement']+i))
            if item is None:break
            window.append(item)
        entry={k:r[k]for k in ('sample_id','case_id','source','round','placement')}
        entry['group']=f"{Path(r['source']).stem}#{r['round']}"
        entry['sample_ids']=[s['sample_id']for s in window]
        entry['observed_rises']=[s['actual_garbage_rise_rows']if s['actual_garbage_rise_rows']is not None else -1 for s in window]
        entry['input_eligible']=all(s['eligible']for s in window)
        entry['action_frames']=[s['action_frame']for s in window]
        entry['lock_frames']=[s['teacher_lock_frame']for s in window]
        entries.append(entry)
        f={k:entry[k]for k in ('sample_id','case_id','source','round','placement','group')}
        f.update(sample_ids=','.join(map(str,entry['sample_ids'])),observed_rises=','.join(map(str,entry['observed_rises'])),input_eligible=int(entry['input_eligible']))
        lines.append(' '.join(f'{k}={v}'for k,v in f.items()))
    a.output.mkdir(parents=True,exist_ok=True)
    plan={'schema_version':1,'root_count':len(entries),'horizon':5,'native_passes':256,'max_negatives':8,
        'feature_names':['roof','ColTrans','RowTrans','HoleLine','WellDepth','HoleDepth','coverWeighted'],
        'original_coefficients':[-128,-160,-160,-380,-100,-40,-1],
        'fixed_offset':'original Evaluation status.value minus original Botris terrain.value at depth5; reward/B2B/pressure/risk/hold are not fitted',
        'negative_selection':'At depth5, exclude any path with hiddenActive/nextMismatch/risen/dead and any exact full teacher path; take highest-score host-verified path per distinct root first move, then at most8 highest branches. Same teacher first move with different later actions is retained and flagged.',
        'limits':'Only first root current+5/HOLD and packet FIFO/deadlines enter teacher replay and native search. Later teacher actions are offline labels; no future queue, attack or garbage holes are supplied. No fit or coefficient choice in this task.',
        'input_hashes':{name:digest(a.input/name)for name in ('roots.log','prepared.json')},'windows':entries}
    (a.output/'manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
    (a.output/'windows.log').write_text('\n'.join(lines)+'\n')
    print(json.dumps({'proposed_roots':len(entries),'groups':sorted({r['group']for r in entries})}))

if __name__=='__main__':main()
