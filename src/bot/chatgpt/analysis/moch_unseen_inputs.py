#!/usr/bin/env python3
"""Reconstruct endpoint-bag membership only from already visible draw history."""
from pathlib import Path
from collections import Counter
import json
import hashlib
import moch_tactical_cases as C

ROOT=C.BASE/'tactical_cases_v2'
TYPES='ijlostz'

def main():
    manifest=json.loads((ROOT/'case_manifest.json').read_text()); selected={(c['source'],c['round']) for c in manifest['selected']}
    states={};counts=Counter();failures=[]
    for source in manifest['source_exports']:
        path=Path(source['path']);assert C.digest(path)==source['sha256']
        for r in json.loads(path.read_text())['rounds']:
            if r['username']!='mochbot-dev':continue
            known={};provenance={};round_states={};seen_bags=set()
            try:
                for p in r['placements']:
                    for phase in ['before','after']:
                        s=p[phase];q=C.visible(s);draws=s['stats']['piecesplaced']+1+int(bool(s['hold']))
                        observations=[]
                        # Only the first lock lacks a preceding spawn snapshot.
                        # An initially filled HOLD can only be the first drawn
                        # piece; empty-HOLD supplied the second as current.
                        if p['index']==1 and phase=='before':
                            if s['hold']:observations.append((1,s['hold']))
                            observations.append((draws,q[0]))
                        # The 'piece' after-snapshot is the newly spawned draw.
                        if phase=='after':observations.append((draws,q[0]))
                        observations += [(draws+i,c) for i,c in enumerate(q[1:],1)]
                        for ordinal,family in observations:
                            if ordinal in known and known[ordinal]!=family:raise ValueError(f'conflicting visible draw {ordinal} at p{p["index"]}/{phase}')
                            known[ordinal]=family;provenance.setdefault(ordinal,{'placement':p['index'],'phase':phase})
                        if draws not in known:raise ValueError('natural current draw absent from observed history')
                        for bag in range((draws+5)//7):
                            if bag in seen_bags:continue
                            begin=bag*7+1
                            if all(i in known for i in range(begin,begin+7)):
                                if set(known[i] for i in range(begin,begin+7))!=set(TYPES):raise ValueError('observed7-draw block is not a tetromino permutation')
                                seen_bags.add(bag);counts['complete_bags_checked']+=1
                        endpoint=draws+5;begin=((endpoint-1)//7)*7+1
                        prefix=[known[i] for i in range(begin,endpoint+1)]
                        if len(set(prefix))!=len(prefix):raise ValueError('duplicate within endpoint-bag prefix')
                        unseen=[c for c in TYPES if c not in prefix]
                        assert len(unseen)==7-(endpoint-begin+1)
                        value={'unseen_types_unordered':unseen,'unseen_native_indices':[TYPES.index(c) for c in unseen],
                            'draws_consumed':draws,'preview_endpoint_draw_ordinal':endpoint,'endpoint_bag_start_draw_ordinal':begin,
                            'observed_endpoint_bag_types_unordered':[c for c in TYPES if c in prefix],
                            'prefix_observation_sources':[{'draw_ordinal':i,**provenance[i]} for i in range(begin,endpoint+1)],
                            'source':'7-bag complement of already-observed draws through current+5 endpoint; raw next[6] never read'}
                        round_states[(p['index'],phase)]=value;counts['snapshots_checked']+=1;counts['unseen_size_'+str(len(unseen))]+=1
                counts['rounds_checked']+=1
                if (r['file'],r['round']) in selected:states[(r['file'],r['round'])]=round_states
            except (ValueError,KeyError,AssertionError) as error:
                failures.append({'source':r['file'],'round':r['round'],'error':str(error)})
    result={'meaning':'types after NEXT endpoint and before next bag boundary; unordered, deduplicated, sorted in native family order; endpoint excluded',
        'native_reference':'src/engine/piece_queue.cpp:77 unseen_pieces_in_preview_endpoint_bag',
        'assumptions':'normal fixed7-bag, empty initial HOLD, no external queue modifications; observed full bags and overlapping previews checked',
        'counts':dict(counts),'failures':failures,'cases':{},'tool_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    for c in manifest['selected']:
        rstates=states.get((c['source'],c['round']));placements=json.loads((ROOT/(c['case_id']+'.json')).read_text())['placements']
        result['cases'][c['case_id']]={'source':c['source'],'round':c['round'],'valid':rstates is not None,
            'placements':{p['index']:{phase:rstates[(p['index'],phase)] for phase in ['before','after']} for p in placements} if rstates else {}}
    (ROOT/'unseen_inputs.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'counts':dict(counts),'failures':failures,'valid_cases':sum(v['valid'] for v in result['cases'].values())},indent=2))
    if failures:raise SystemExit(1)
if __name__=='__main__':main()
