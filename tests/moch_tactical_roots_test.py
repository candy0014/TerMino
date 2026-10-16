#!/usr/bin/env python3
"""Regression for direct no-clear spin labels and independent clear audit."""
import importlib.util
from pathlib import Path
import sys
import unittest

ANALYSIS=Path(__file__).resolve().parents[1]/'src/bot/chatgpt/analysis'
sys.path.insert(0,str(ANALYSIS))
import moch_tactical_roots as roots

def lock(flags,clear=None):
    counters={'singles':0,'tspinsingles':0,'minitspinsingles':0}
    before={'lines':0,'clears':counters}
    after={'lines':int(clear is not None),'clears':dict(counters)}
    if clear:after['clears'][clear]=1
    return {'index':1,'piece':{'flags':flags},
            'before':{'audit':{'falling':{'flags':flags}},'snapshot':{'stats':before}},
            'after':{'snapshot':{'stats':after}}}

class SpinLabelTests(unittest.TestCase):
    def test_no_clear_rotation_labels_are_observed(self):
        for flags,expected in [(0,(False,False)),(16,(False,False)),
                               (8,(True,False)),(24,(True,True)),(28953,(True,True))]:
            self.assertEqual(roots.observed_spin(flags),expected)
        result=roots.audit_spin_labels([lock(8),lock(24),lock(0)])
        self.assertEqual(result['no_clear_labels_observed'],3)
        self.assertEqual(result['no_clear_spin_locks'],2)
        self.assertEqual(result['no_clear_mini_locks'],1)
    def test_clear_counters_independently_validate_flags(self):
        result=roots.audit_spin_labels([lock(0,'singles'),lock(8,'tspinsingles'),lock(24,'minitspinsingles')])
        self.assertEqual(result['clear_labels_checked'],3)
        for sample in [lock(0,'tspinsingles'),lock(8,'minitspinsingles'),lock(24,'tspinsingles')]:
            with self.assertRaisesRegex(ValueError,'spin_flags_clear_stats_mismatch'):
                roots.audit_spin_labels([sample])
    def test_distinct_samples_and_unsupported_rules_fail_closed(self):
        sample=lock(8);sample['piece']['flags']=0
        with self.assertRaisesRegex(ValueError,'lock_flag_mismatch'):roots.audit_spin_labels([sample])
        with self.assertRaisesRegex(ValueError,'unsupported_zenith_duo'):roots.observed_spin(32)
        with self.assertRaisesRegex(ValueError,'invalid_lock_flags'):roots.observed_spin(-1)

if __name__=='__main__':unittest.main()
