import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_frontend_acceptance import validate


class NativeAcceptanceGateTest(unittest.TestCase):
    def report(self):
        # Synthetic fixtures validate the gate; they are never native evidence.
        checks = {key: 'passed' for key in ('build', 'regression', 'keyboard', 'focus', 'network_replay',
                  'data_upgrade', 'ime', 'dpi', 'gpu_driver')}
        return dict(source_commit='a' * 40, cases=[dict(platform=p, frontend=f, environment='native',
                    status='passed', evidence=['synthetic fixture'], checks=dict(checks))
                    for p in ('Linux', 'Windows') for f in ('terminal', 'opengl')])

    def test_all_four_native_cases_required(self):
        report = self.report();self.assertTrue(validate(report, 'a' * 40))
        report['cases'].pop()
        with self.assertRaises(ValueError):validate(report, 'a' * 40)

    def test_wine_cannot_be_promoted_to_native_acceptance(self):
        report = self.report();report['cases'][2]['environment'] = 'Wine'
        with self.assertRaises(ValueError):validate(report, 'a' * 40)

    def test_injected_text_does_not_replace_native_ime(self):
        report = self.report();report['cases'][1]['checks']['ime'] = 'pending'
        with self.assertRaises(ValueError):validate(report, 'a' * 40)

    def test_empty_evidence_or_pending_source_is_rejected(self):
        report = self.report();report['cases'][0]['evidence'] = []
        with self.assertRaises(ValueError):validate(report, 'a' * 40)
        report = self.report();report['source_commit'] = None
        with self.assertRaises(ValueError):validate(report, 'a' * 40)


if __name__ == '__main__':
    unittest.main()
