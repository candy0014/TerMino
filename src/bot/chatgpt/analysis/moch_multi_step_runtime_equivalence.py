#!/usr/bin/env python3
"""Check the actual runtime wrapper against all frozen path boards/model dots."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--binary',type=Path,default=Path('build/bot_chatgpt_moch_multi_step_test'))
    ap.add_argument('--dataset',type=Path,default=Path('runlogs/chatgpt_moch_resume_20260926/multi_step_pilot/dataset.jsonl'))
    ap.add_argument('--model',type=Path,default=Path('runlogs/chatgpt_moch_resume_20260926/multi_step_pilot_fit/fit_artifacts/model.json'))
    ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    model=json.loads(a.model.read_text());meta=subprocess.check_output([str(a.binary),'--metadata'],text=True).split()
    assert meta==[digest(a.model),model['registration_sha256'],'0.5']
    assert model['delta']==[-0.5]*7
    assert model['positive_penalties']==[w*0.5 for w in model['original_positive_penalties']]
    boards=[];sources=[]
    for line in a.dataset.read_text().splitlines():
        r=json.loads(line)
        if not r['eligible']:continue
        for role,path in [('teacher',r['teacher'])]+[(f'negative_{i}',p)for i,p in enumerate(r['negatives'])]:
            for step in path['steps']:
                boards.append((step['rows'],step['score']['features'],step['score']['terrain_value']))
                sources.append({'sample_id':r['sample_id'],'role':role,'depth':step['depth']})
    assert len(boards)==5265
    boards.append(([0]*40,[0,80,20,0,0,0,0],-16000));sources.append({'case':'empty_board'})
    input_text=''.join(str(i)+' '+' '.join(map(str,row))+'\n'for i,(row,_,_)in enumerate(boards))
    output=subprocess.check_output([str(a.binary),'--boards'],input=input_text,text=True)
    max_error=0;exact=0
    for i,line in enumerate(output.splitlines()):
        index,old,new=line.split();assert int(index)==i
        _,features,original=boards[i];expected=-sum(w*f for w,f in zip(model['positive_penalties'],features))
        assert float(old)==original
        error=abs(float(new)-expected);max_error=max(max_error,error);exact+=error==0
        assert error<=1e-7
    assert len(output.splitlines())==len(boards)
    a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'boards.in').write_text(input_text);(a.output/'runtime.out').write_text(output)
    result={'path_boards':5265,'empty_boards':1,'total':len(boards),'bitwise_model_dot_matches':exact,'max_abs_error':max_error,
        'model_sha256':digest(a.model),'registration_sha256':model['registration_sha256'],'dataset_sha256':digest(a.dataset),'binary_sha256':digest(a.binary),
        'tool_sha256':digest(Path(__file__)),'scope':'Runtime eval wrapper matches frozen seven-term model on every exported path board plus empty; rule-state checks are in the C++ regression.'}
    (a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n');(a.output/'sources.json').write_text(json.dumps(sources,indent=2)+'\n')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
