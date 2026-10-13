import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('spin_report', ROOT / 'src/bot/chatgpt/analysis/spin_report.py')
report = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(report)


class SpinReportTest(unittest.TestCase):
    def log(self):
        return '\n'.join([
            'TETR_BOT_DIAGNOSTIC side=first trace_version=1 pieces=0 root_b2b=3 selected=0',
            'TETR_TEST_PLACEMENT side=first total_pieces=1 piece=Ｉ lines=0 spin=0 mini=0 attack=0 b2b_before=3 b2b_after=3',
            'TETR_BOT_DIAGNOSTIC side=first trace_version=1 pieces=1 root_b2b=3 selected=1',
            'TETR_TEST_PLACEMENT side=first total_pieces=2 piece=Ｊ lines=1 spin=0 mini=0 attack=0 b2b_before=3 b2b_after=0',
            'TETR_LOCAL_VERSUS_RESULT first_pieces=2 second_pieces=3'])

    def test_extract_break_and_prior_visible_roots(self):
        self.assertEqual([r['root']['pieces'] for r in report.extract(self.log())], ['1'])
        roots = report.extract(self.log(), 2)
        self.assertEqual([r['root']['pieces'] for r in roots], ['0', '1'])
        self.assertEqual(roots[0]['breaks'], [1])
        self.assertEqual(len(report.extract(self.log().replace(
            'TETR_LOCAL_VERSUS_RESULT first_pieces=2 second_pieces=3',
            'TETR_TEST_RESULT side=first pieces=2'))), 1)

    def test_incomplete_and_inconsistent_logs_rejected(self):
        for bad in [self.log().rsplit('\n', 1)[0], self.log().replace('root_b2b=3', 'root_b2b=2'),
                    self.log().replace('total_pieces=2', 'total_pieces=3'),
                    self.log().replace('piece=Ｊ', 'piece=Ｔ'),
                    self.log().replace('trace_version=1', 'trace_version=0')]:
            with self.assertRaises(ValueError):
                report.extract(bad)
        with self.assertRaises(ValueError):
            report.fields('TAG x=1 x=2')

    def test_score_gain_is_not_dominance_or_victory(self):
        state = dict(verified=True, depth=1, spins=2, broken=False, value=100,
                     roof=8, peak_roof=9, holes=1, incoming=0, risk=1, attack=5, b2b=4,
                     path=[dict(production_geometry=True, production_spin_variant=True)])
        branch = lambda best=None, two=None: dict(best=best, two_spins=two, prefix=two)
        row = dict(horizon=1, live_first=branch(dict(state, value=90, attack=6)),
                   free=branch(), preserve=branch(state, state))
        c = report.classify(row)
        self.assertTrue(c['higher_current_score'])
        self.assertFalse(c['higher_score_no_worse_measured_state'])
        row['live_first'] = branch()
        self.assertFalse(report.classify(row)['comparable'])
        row['preserve']['two_spins'] = dict(state, broken=True)
        with self.assertRaises(ValueError):
            report.classify(row)


if __name__ == '__main__':
    unittest.main()
