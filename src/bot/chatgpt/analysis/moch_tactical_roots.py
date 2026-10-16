#!/usr/bin/env python3
"""Prepare audited first-action roots; preserve all excluded cases and stages."""
from pathlib import Path
from collections import Counter
import json
import hashlib
from moch_imitation import official_cells, masks

BASE=Path('runlogs/chatgpt_moch_20260926/tactical_cases_v2');OUT=BASE/'pressure_inputs'
TYPES='ijlostz'
def observed_spin(flags):
    """Reviewed engine R.IsSpin/IsSpinMini, sampled before stack attachment."""
    if not isinstance(flags, int) or flags < 0:
        raise ValueError('invalid_lock_flags')
    if flags & 32:
        raise ValueError('unsupported_zenith_duo_spin_all')
    return bool(flags & 8), flags & 24 == 24

def audit_spin_labels(observations):
    """Check direct flags against independent clear counters on every lock."""
    counts=Counter()
    for p in observations:
        flags=p['before']['audit']['falling']['flags']
        if flags!=p['piece']['flags']:
            raise ValueError(f"lock_flag_mismatch at p{p['index']}")
        spin,mini=observed_spin(flags)
        before=p['before']['snapshot']['stats'];after=p['after']['snapshot']['stats']
        lines=after['lines']-before['lines']
        clears={key:value-before['clears'][key] for key,value in after['clears'].items()
                if value!=before['clears'][key]}
        counts['locks']+=1
        counts['spin_locks']+=spin;counts['mini_locks']+=mini
        if lines:
            stats_spin=any('spin' in key for key in clears)
            stats_mini=any(key.startswith('mini') for key in clears)
            if (spin,mini)!=(stats_spin,stats_mini):
                raise ValueError(f"spin_flags_clear_stats_mismatch at p{p['index']}: {flags}, {clears}")
            counts['clear_labels_checked']+=1
        else:
            counts['no_clear_labels_observed']+=1
            counts['no_clear_spin_locks']+=spin
            counts['no_clear_mini_locks']+=mini
    return dict(counts)

def main():
    manifest=json.loads((BASE/'case_manifest.json').read_text());capture=json.loads((OUT/'manifest.json').read_text());rounds={}
    for r in capture['rounds']:
        data=json.loads((OUT/r['output']).read_text());rounds[r['file'],r['round']]={p['index']:p for p in data['placements']}
    spin_counts=Counter()
    for observations in rounds.values():spin_counts.update(audit_spin_labels(observations.values()))
    result=[];roots=[];counts=Counter()
    for case in manifest['selected']:
        original=json.loads((BASE/(case['case_id']+'.json')).read_text());by_index={p['index']:p for p in original['placements']};observations=rounds[case['source'],case['round']]
        for index in range(case['start_index']-case.get('context_before',6),
                           case['end_index']+case.get('context_after',3)+1):
            sample=len(result);p=by_index[index];o=observations[index];a=o.get('first_action');reasons=[]
            record={'sample_id':sample,'case_id':case['case_id'],'source':case['source'],'round':case['round'],'placement':index,
                'phase':'preparation' if index<case['start_index'] else 'aftermath' if index>case['end_index'] else 'event',
                'pressure_trigger':index==case.get('pressure_trigger_index'),'teacher_lock_frame':p['frame'],
                'before_first_action':a,'teacher_actual_after_rows':p['after']['rows'],'teacher_delta':p['delta']}
            if not a:reasons.append('no_first_action')
            else:
                s=a['snapshot'];mapping=a['pressure']['mapping'];spawns=o['spawns'];audit=a['audit']
                reasons+=mapping['reasons']
                if not spawns:reasons.append('no_spawn_audit')
                else:
                    spawn=spawns[0]
                    if audit['holds']!=spawn['audit']['holds']:reasons.append('hold_before_first_action')
                    if audit['inputs']!=spawn['audit']['inputs']:reasons.append('input_before_first_action')
                    if spawn['frame']>a['frame']:reasons.append('spawn_after_action')
                if audit['holdlocked']:reasons.append('hold_already_locked')
                if audit['falling']['r']!=0 or audit['falling']['x']!=4:reasons.append('moved_or_rotated_before_first_action')
                if not audit['playing']:reasons.append('not_playing')
                if s['rows']!=p['before']['rows']:reasons.append('board_changes_between_action_and_lock')
                if len(s['visible_next'])!=6:reasons.append('visible_queue_not_current_plus5')
                if mapping['incoming']!=s['pending']:reasons.append('pending_sum_mismatch')
                held=o['before']['audit']['holds']-audit['holds'];record['teacher_hold']=held==1
                if held not in [0,1]:reasons.append('teacher_hold_count_not_zero_or_one')
                spin,mini=observed_spin(o['before']['audit']['falling']['flags'])
                if p['delta']['lines'] and (spin,mini)!=(p['spin'],any(k.startswith('mini') for k in p['delta']['clears'])):
                    reasons.append('original_clear_spin_label_mismatch')
                cells=official_cells(p['piece']);attached=[list(row) for row in s['rows']]
                for x,y in cells:
                    if attached[y][x]!='.':reasons.append('teacher_overlaps_stack')
                    attached[y][x]=p['piece']['type']
                remaining=[''.join(row) for row in attached if '.' in row];lines=40-len(remaining);pure=['.'*10]*lines+remaining
                if lines!=p['delta']['lines']:reasons.append('teacher_pure_clear_count_mismatch')
                # Retrospective validation only; actual garbage holes never enter
                # the root or the search. Rising shifts the pure board upward.
                actual=p['after']['rows'];rises=[]
                for n in range(9):
                    if actual[:40-n]==pure[n:] and all(set(row)<=set('.#') and '#' in row for row in actual[40-n:]):rises.append(n)
                if len(rises)!=1:reasons.append('actual_after_not_unique_pure_clear_plus_up_to8_garbage')
                record.update(teacher_cells=sorted([x,39-y] for x,y in cells),teacher_family=TYPES.index(p['piece']['type']),teacher_lines=lines,
                    teacher_spin=spin,teacher_mini=mini,teacher_lock_flags=o['before']['audit']['falling']['flags'],
                    teacher_spin_label_source='lock-before observed flags: spin=flags&8; mini=(flags&24)==24',
                    teacher_spin_observed=True,teacher_pure_after_rows=pure,teacher_pure_after_masks=masks(pure),
                    actual_garbage_rise_rows=rises[0] if len(rises)==1 else None,
                    teacher_b2b_after=p['after']['stats']['btb'],teacher_combo_after=p['after']['stats']['combo'],
                    action_frame=a['frame'],teacher_action_delay_seconds=(p['frame']-a['frame'])/60,
                    unseen_native_indices=[TYPES.index(c) for c in a['unseen']['types']])
                if not reasons:
                    garbage_height=max((40-y for y,row in enumerate(s['rows']) if '#' in row),default=0)
                    f={'trace_version':1,'side':'first','sample_id':sample,'pieces':index-1,'board':','.join(map(str,masks(s['rows']))),
                        'root_next':','.join(str(TYPES.index(c)) for c in s['visible_next']),'root_hold':TYPES.index(s['hold']) if s['hold'] else -1,
                        'root_unseen':','.join(map(str,record['unseen_native_indices'])) or '-',
                        'root_row':-2,'root_col':4,'root_rotation':0,'root_b2b':s['stats']['btb'],'root_combo':s['stats']['combo'],
                        'root_garbage_height':garbage_height,'root_cleared':int(observations[index-1]['after']['snapshot']['stats']['lines']>observations[index-1]['before']['snapshot']['stats']['lines']),
                        'incoming':mapping['incoming'],'ready':mapping['readyIncoming'],'forecast_step':1,'root_packets':','.join(f"{p['lines']}:{p['secondsUntilReady']:.17g}" for p in mapping['packets']) or '-',
                        'opener_double':1,'selected':TYPES.index(s['visible_next'][0]),'row':-2,'col':4,'rotation':0,
                        'teacher_family':record['teacher_family'],'teacher_hold':int(record['teacher_hold']),
                        'teacher_cells':','.join(str(y*10+x) for x,y in record['teacher_cells']),
                        'teacher_after':','.join(map(str,record['teacher_pure_after_masks'])),
                        'teacher_spin':int(record['teacher_spin']),'teacher_mini':int(record['teacher_mini']),'teacher_spin_known':int(record['teacher_spin_observed']),
                        'teacher_lines':lines,'teacher_attack':p['delta']['attack'],'teacher_b2b':record['teacher_b2b_after'],'teacher_combo':record['teacher_combo_after']}
                    roots.append('TETR_BOT_DIAGNOSTIC '+' '.join(f'{k}={v}' for k,v in f.items()))
            record['eligible']=not reasons;record['reasons']=reasons;result.append(record);counts['total']+=1;counts['eligible']+=not reasons;counts.update(reasons)
    (OUT/'roots.log').write_text('\n'.join(roots)+'\n');(OUT/'prepared.json').write_text(json.dumps({'records':result,'counts':dict(counts),'spin_label_audit':{'counts':dict(spin_counts),'reference_sha256':capture['reference_sha256'],'source':'Reviewed engine R.ROTATION_SPIN=8, R.ROTATION_MINI=16; IsSpinMini requires both bits; lock event is before attachment/clear. All captured lock flags match piece flags, all clear flags match independent cumulative clear counters.'},'after_stage':'teacher_pure_after reconstructed and checked against actualafter plus retrospective rise; hole rows excluded from BOT input','combo':'reviewed v19 stats.combo and host are both0 at no combo; direct copy, no+1','forecast_step':1,'tool_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},indent=2)+'\n');print(json.dumps({'roots':dict(counts),'spin_labels':dict(spin_counts)},indent=2))
if __name__=='__main__':main()
