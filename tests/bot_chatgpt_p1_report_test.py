import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src/bot/chatgpt/analysis'))
import p1_report as report


class P1ReportTest(unittest.TestCase):
    def log(self):
        rows = []
        for i in range(40):
            spin, broken = i % 4 == 1, i % 4 == 2
            incoming = 10 if i % 6 == 0 else 0
            rows.append(f'TETR_BOT_DIAGNOSTIC side=first trace_version=1 pieces={i} '
                        f'root_b2b=5 selected=0 incoming={incoming} ready={incoming} roof=6 root_hold=-1')
            rows.append(f'TETR_TEST_PLACEMENT side=first total_pieces={i + 1} piece=Ｉ '
                        f'lines={int(spin or broken)} spin={int(spin)} mini=0 attack=1 '
                        f'b2b_before=5 b2b_after={0 if broken else 6 if spin else 5}')
        return '\n'.join(rows + ['TETR_LOCAL_VERSUS_RESULT first_pieces=40 second_pieces=52'])

    def row(self):
        low = dict(move=[0, 19, 0, 0, 0, 0], hold=0, value=100, depth=6, attack=4,
                   b2b=2, risen=0, dead=0, passes=10, path=[])
        high = dict(low, move=[0, 19, 1, 0, 0, 0], value=200, passes=40,
                    path=[dict(created_pass=1, expanded_pass=20, depth=1, sibling_rank=5,
                               risen=0, hidden_active=0, next_mismatch=0),
                          dict(created_pass=20, expanded_pass=0, depth=2, sibling_rank=1,
                               risen=0, hidden_active=0, next_mismatch=0)])
        return dict(missing_root_geometry=0, traced=[low, high], plain=copy.deepcopy([low, high]))

    def test_balanced_unique_selection(self):
        samples = report.select([(Path(f'source{i}'), self.log()) for i in range(3)])
        self.assertEqual(len(samples), 30)
        self.assertEqual(len({(s['source'], s['root']['pieces']) for s in samples}), 30)
        self.assertEqual(set(report.Counter(s['group'] for s in samples).values()), {6})
        self.assertEqual(samples, report.select([(Path(f'source{i}'), self.log()) for i in range(3)]))

    def test_incomplete_input_rejected(self):
        with self.assertRaises(ValueError):
            report.select([(Path('one'), self.log())])
        with self.assertRaises(ValueError):
            report.select([(Path(str(i)), self.log().rsplit('\n', 1)[0]) for i in range(3)])

    def test_search_witness_is_same_horizon_only(self):
        row = self.row()
        self.assertTrue(report.classify(row)['search_allocation_witness'])
        self.assertEqual(report.classify(row)['delayed_prefix_depth'], 1)
        row['traced'][1]['depth'] = 7
        self.assertFalse(report.classify(row)['search_allocation_witness'])
        self.assertIsNone(report.classify(row)['value_delta'])

    def test_fixed_equivalence_validation(self):
        row = self.row()
        report.validate_fixed([row])
        row['plain'][1]['value'] += 1
        with self.assertRaises(ValueError):
            report.validate_fixed([row])

    def test_repeat_comparison_ignores_only_time(self):
        self.assertEqual(report.without_time({'ms': 5, 'a': [{'ms': 9, 'value': 1}]}),
                         {'a': [{'value': 1}]})

    def test_live_rule_audit_requires_same_first_move(self):
        sample = dict(root=dict(selected='0', row='19', col='0', rotation='0'),
                      event=dict(spin='0', mini='0', lines='0', attack='0', b2b_after='2'))
        row = self.row()
        row['traced'][0]['path'] = [dict(lines=0, attack=0, b2b=2)]
        self.assertEqual(report.audit_live_first(sample, row),
                         dict(live_first_matches=True, live_first_rule_mismatch=False))
        row['traced'][0]['path'][0]['attack'] = 1
        self.assertTrue(report.audit_live_first(sample, row)['live_first_rule_mismatch'])
        sample['root']['col'] = '4'
        self.assertIsNone(report.audit_live_first(sample, row)['live_first_rule_mismatch'])


if __name__ == '__main__':
    unittest.main()
