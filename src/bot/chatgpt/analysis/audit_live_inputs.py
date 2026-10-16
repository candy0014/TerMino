#!/usr/bin/env python3
"""Read only finalized live-game artifacts; no searches, replays or tuning."""
import argparse
from collections import Counter
from datetime import datetime,timezone
import hashlib
import json
import math
from pathlib import Path

TYPES=set(range(7))
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def integers(value):return [] if value=='-' else [int(x) for x in value.split(',')]
def require(test,label,piece=None):
    if not test:raise ValueError(f'{label} at piece {piece}')

def audit_game(decisions,placements,candidate):
    counts=Counter();known={};previous=None
    roots=decisions['first'];events=placements['first']
    require(len(roots)==len(events),'root_placement_count')
    require(bool(roots),'empty_game')
    for i,(root,event) in enumerate(zip(roots,events)):
        require(int(root['pieces'])==i and int(event['total_pieces'])==i+1,'piece_order',i)
        queue=integers(root['root_next']);unseen=integers(root['root_unseen']);hold=int(root['root_hold']);selected=int(root['selected'])
        require(len(queue)==6 and all(x in TYPES for x in queue),'current_plus_five',i)
        require(unseen==sorted(set(unseen)) and set(unseen)<=TYPES and len(unseen)<=6,'unseen_valid_set',i)
        require(hold in [-1,*range(7)],'enabled_standard_hold',i)
        require(selected in [queue[0],hold if hold>=0 else queue[1]],'selected_visible_or_hold',i)
        draw=i+1+int(hold>=0)
        for offset,family in enumerate(queue):
            ordinal=draw+offset
            require(ordinal not in known or known[ordinal]==family,'overlapping_visible_history',i)
            known[ordinal]=family
        endpoint=draw+5;start=(endpoint-1)//7*7+1
        prefix=[known[j] for j in range(start,endpoint+1)]
        require(len(set(prefix))==len(prefix),'endpoint_bag_duplicate',i)
        require(unseen==sorted(TYPES-set(prefix)),'unseen_observed_history_complement',i)
        # Every newly completed seven-draw window is checked independently.
        if endpoint%7==0:
            require(set(known[j] for j in range(endpoint-6,endpoint+1))==TYPES,'complete_visible_bag',i)
            counts['complete_visible_bags']+=1
        expected=unseen[0] if candidate and len(unseen)==1 else -1
        require(int(root['inferred_next'])==expected,'inferred_singleton_only',i)
        packets=[] if root['root_packets']=='-' else [(int(lines),float(seconds)) for lines,seconds in (item.split(':') for item in root['root_packets'].split(','))]
        require(all(n>0 and math.isfinite(t) for n,t in packets),'valid_packet',i)
        incoming=int(root['incoming']);ready=int(root['ready']);total=sum(n for n,t in packets)
        require(total==incoming,'packet_sum',i)
        prefix_ready=0;blocked=False;ambiguous=False;last=-math.inf
        for n,t in packets:
            require(t+1e-10>=last,'packet_fifo_deadline_order',i);last=t
            ambiguous|=abs(t)<1e-10
            if t>0:blocked=True
            if not blocked:prefix_ready+=n
        if ambiguous:
            low=0;high=0;low_block=False;high_block=False
            for n,t in packets:
                low_block|=t>=-1e-10;high_block|=t>1e-10
                if not low_block:low+=n
                if not high_block:high+=n
            require(low<=ready<=high,'packet_ready_boundary_range',i)
            counts['floating_ready_boundaries']+=1
        else:require(prefix_ready==ready,'packet_ready_fifo_prefix',i)
        board=integers(root['board']);height=int(root['root_garbage_height']);roof=max((y+1 for y,bits in enumerate(board) if bits),default=0)
        require(len(board)==40 and all(0<=row<1024 for row in board),'board_masks',i)
        require(int(root['roof'])==roof and 0<=height<=roof,'garbage_height_bounds',i)
        require(int(root['root_b2b'])==int(event['b2b_before']),'root_b2b_event',i)
        if i==0:require(height==0 and hold==-1 and int(root['root_combo'])==0,'initial_empty_root',i)
        if previous:
            prior,oldqueue,oldhold,oldselected,oldevent,oldready=previous
            changed=oldselected!=oldqueue[0];consumed=2 if changed and oldhold<0 else 1
            require(queue[:6-consumed]==oldqueue[consumed:],'next_queue_transition',i)
            require(hold==(oldqueue[0] if changed else oldhold),'hold_transition',i)
            priorlines=int(oldevent['lines']);rise=min(8,oldready) if priorlines==0 else 0
            require(height==int(prior['root_garbage_height'])-int(oldevent['garbage_lines'])+rise,'garbage_height_transition',i)
            require(int(root['root_b2b'])==int(oldevent['b2b_after']),'b2b_transition',i)
            require(int(root['root_combo'])==(int(prior['root_combo'])+1 if priorlines else 0),'combo_transition',i)
            counts['adjacent_transitions']+=1
        previous=(root,queue,hold,selected,event,ready)
        counts['roots']+=1;counts['singleton_roots']+=len(unseen)==1;counts['inferred_roots']+=expected>=0
        counts['with_packets']+=bool(packets);counts['with_ready']+=ready>0;counts['with_garbage_height']+=height>0
        counts['hold_selected']+=selected!=queue[0]
    return dict(counts)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,default=Path('runlogs/chatgpt_moch_resume_20260926'))
    ap.add_argument('--output',type=Path)
    ap.add_argument('--variants',nargs='+',default=['baseline_inputdiag','known_bag_v1'])
    ap.add_argument('--opponents',nargs='+',choices=['1','4'],default=['1','4'])
    args=ap.parse_args();output=args.output or args.root/'live_input_audit.json'
    report={'scope':'Snapshot of finalized game files only; unfinished or absent groups are not certified complete.',
            'timestamp_utc':datetime.now(timezone.utc).isoformat(),'groups':{},'failures':[],
            'limits':['Unseen is rebuilt exclusively from prior/current visible root_next values, without hidden queue/RNG.',
                      'Garbage height is checked from an empty initial field through actual cleared-garbage counts and known ready-rise rules; masks alone contain no garbage color labels.',
                      'A deadline within 1e-10 seconds of zero receives a floating-boundary interval check and is counted separately.'],
            'tool_sha256':digest(Path(__file__))}
    total=Counter()
    for name in ['dev9_'+model+'_vs_'+opponent for model in args.variants for opponent in args.opponents]:
        directory=args.root/name;summary=directory/'results.jsonl'
        group={'exists':directory.exists(),'finalized_games':0,'natural_games':0,'truncated_games':0,'games':[],'counts':Counter()}
        report['groups'][name]=group
        if not summary.exists():continue
        metadata=directory/'metadata.json'
        if metadata.exists():
            m=json.loads(metadata.read_text())
            group.update(metadata_sha256=digest(metadata),binary=m['binary'],binary_sha256=m['binary_sha256'])
        rows=[]
        for line in summary.read_text().splitlines():
            try:row=json.loads(line)
            except json.JSONDecodeError:group['partial_summary_line_ignored']=True;continue
            if row['side']=='first':rows.append(row)
        for row in rows:
            path=directory/f"{row['case']}_seed{row['seed']}.decisions.json";place=path.with_name(path.name.replace('.decisions.json','.placements.json'))
            try:
                decisions=json.loads(path.read_text());placements=json.loads(place.read_text())
            except (FileNotFoundError,json.JSONDecodeError):
                group.setdefault('not_yet_finalized_files',[]).append(str(path));continue
            game={'seed':row['seed'],'decisions_file':str(path),'decisions_sha256':digest(path),'placements_sha256':digest(place),
                  'truncated':row.get('truncated'),'termination_reason':row.get('termination_reason')}
            group['games'].append(game);group['finalized_games']+=1
            group['natural_games']+=row.get('termination_reason')=='natural';group['truncated_games']+=bool(row.get('truncated'))
            try:
                require(len(decisions['first'])==int(row['diagnostic_decisions'])==int(row['pieces']),'summary_count')
                game['counts']=audit_game(decisions,placements,'known_bag_v1' in name)
                group['counts'].update(game['counts']);total.update(game['counts']);game['passed']=True
            except (ValueError,KeyError,TypeError) as error:
                game['passed']=False;game['error']=str(error);report['failures'].append({'group':name,'seed':row['seed'],'error':str(error)})
        group['planned_natural_games_complete']=group['natural_games']>=9
    report['counts']=dict(total)
    report['all_finalized_inputs_pass']=not report['failures']
    report['all_planned_groups_complete']=all(g.get('planned_natural_games_complete',False) for g in report['groups'].values())
    output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['counts','all_finalized_inputs_pass','all_planned_groups_complete','failures']},indent=2))
    if report['failures']:raise SystemExit(1)

if __name__=='__main__':main()
