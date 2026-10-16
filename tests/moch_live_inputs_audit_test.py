#!/usr/bin/env python3
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/bot/chatgpt/analysis'))
from audit_live_inputs import audit_game

def example(candidate=True):
    root={'pieces':'0','root_next':'0,1,2,3,4,5','root_unseen':'6','root_hold':'-1','selected':'0',
          'inferred_next':'6' if candidate else '-1','root_packets':'3:-1,2:0.3','incoming':'5','ready':'3',
          'board':','.join(['0']*40),'root_garbage_height':'0','roof':'0','root_b2b':'0','root_combo':'0'}
    placement={'total_pieces':'1','b2b_before':'0','b2b_after':'0','lines':'0','garbage_lines':'0'}
    return {'first':[root]},{'first':[placement]}

class LiveAuditTests(unittest.TestCase):
    def test_valid_known_and_baseline(self):
        for candidate in [True,False]:
            d,p=example(candidate);result=audit_game(d,p,candidate)
            self.assertEqual(result['roots'],1);self.assertEqual(result['inferred_roots'],int(candidate))
    def test_semantic_corruptions_rejected(self):
        for key,value,label in [('root_unseen','0','unseen_observed_history_complement'),
             ('root_unseen','6,6','unseen_valid_set'),('inferred_next','0','inferred_singleton_only'),
             ('root_next','0,1,2,3,4,5,6','current_plus_five'),('incoming','6','packet_sum'),
             ('ready','5','packet_ready_fifo_prefix'),('root_packets','3:0.3,2:-1','packet_fifo_deadline_order'),
             ('root_garbage_height','1','garbage_height_bounds'),('selected','6','selected_visible_or_hold')]:
            d,p=example();d['first'][0][key]=value
            with self.assertRaisesRegex(ValueError,label):audit_game(d,p,True)

if __name__=='__main__':unittest.main()
