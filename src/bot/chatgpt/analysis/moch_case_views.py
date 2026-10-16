#!/usr/bin/env python3
"""Clean interactive-view data; no hidden ordered queue fields, no BOT inputs."""
from pathlib import Path
import json
from moch_imitation import official_cells
import moch_tactical_cases as C

ROOT=C.BASE/'tactical_cases_v2'
def snapshot(s, unseen):
    q=C.visible(s)
    return {'rows_top_to_bottom':s['rows'],'current':q[0],'next_visible':q[1:],
        'hold':s['hold'],'unseen_bag_types_unordered':unseen['unseen_types_unordered'],
        'unseen_bag_native_indices':unseen['unseen_native_indices'],'unseen_provenance':unseen,
        'pending_total':s['pending'],'ready_total':None,'pending_packets':None,
        'b2b':s['stats']['btb'],'combo':s['stats']['combo'],'garbage_counters':s['stats']['garbage'],
        'height':s['terrain']['maxHeight'],'garbage_rows':s['terrain']['garbageRows']}
def main():
    manifest=json.loads((ROOT/'case_manifest.json').read_text());unseen=json.loads((ROOT/'unseen_inputs.json').read_text());records={}
    for source in manifest['source_exports']:
        for r in json.loads(Path(source['path']).read_text())['rounds']:
            if r['username']=='mochbot-dev':records[(r['file'],r['round'])]=r
    cases=[]
    for c in manifest['selected']:
        raw=json.loads((ROOT/(c['case_id']+'.json')).read_text());r=records[(c['source'],c['round'])];indexed={p['index']:p for p in r['placements']};placements=[]
        for p in raw['placements']:
            i=p['index'];phase='preparation' if i<c['start_index'] else 'aftermath' if i>c['end_index'] else 'event';u=unseen['cases'][c['case_id']]['placements'][str(i)]
            before=snapshot(p['before'],u['before']);after=snapshot(p['after'],u['after']);cells=official_cells(p['piece']);previous=indexed.get(i-1)
            prior=None
            if previous:
                prior={'placement':i-1,'frame':previous['frame'],'current_and_next':C.visible(previous['after']),
                    'hold':previous['after']['hold'],'sampling':'after previous lock/new piece event; evidence only, may precede later garbage arrivals and HOLD',
                    'is_complete_prehold_pressure_root':False}
            placements.append({'placement':i,'phase':phase,'pressure_trigger':i==c.get('pressure_trigger_index'),
                'frame':p['frame'],'replay_seconds':p['frame']/60,'clock':'official replay frame /60; not wall time',
                'player':{'username':'mochbot-dev','side':r['side']},
                'before':before,'after':after,'before_sampling':'lock callback after HOLD and movement',
                'prior_spawn_evidence':prior,'prehold_search_root':None,'search_input_ready':False,
                'missing_search_inputs':['validated pre-HOLD sampling instant','visible FIFO readiness and relative deadlines'],
                'teacher':{'family':p['piece']['type'],'official_lock':p['piece'],
                    'cells_bottom_origin':sorted([x,39-y] for x,y in cells),'spin':p['spin'],
                    'mini':any(k.startswith('mini') for k in p['delta']['clears']),
                    'lines':p['delta']['lines'],'attack':p['delta']['attack'],'sent':p['delta']['sent']},
                'delta':p['delta']})
        assert sum(p['phase']=='preparation' for p in placements)==6 and sum(p['phase']=='aftermath' for p in placements)==3
        cases.append({'case':c,'source':{'file':c['source'],'round':c['round'],'side':r['side'],'username':'mochbot-dev','validation':r['validation']},'placements':placements})
    result={'schema_version':1,'purpose':'case-study visualization only; not a complete pressure BOT input',
        'source_manifest':'case_manifest.json','unseen_source':'unseen_inputs.json',
        'limits':['Selected strong-play cases are not population/matching-rate samples','pending_total is not ready_total','net garbageRowsAdded is not received','next_visible contains exactly5; no raw ordered hidden preview included','before is post-HOLD; prior_spawn_evidence may precede intervening garbage'],
        'cases':cases}
    (ROOT/'case_views.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'cases':len(cases),'placements':sum(len(c['placements']) for c in cases),'all_search_inputs_ready':False}))
if __name__=='__main__':main()
