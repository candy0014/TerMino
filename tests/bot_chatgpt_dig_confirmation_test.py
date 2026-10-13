import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('dig_confirmation_report',
    Path(__file__).resolve().parents[1] / 'src/bot/chatgpt/dig_confirmation_report.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class DigConfirmationTest(unittest.TestCase):
    def test_pooled_ratio(self):
        rows = [dict(pieces=200, garbage_lines=100), dict(pieces=150, garbage_lines=50)]
        self.assertEqual(m.ratio(rows), 350/150)
        with self.assertRaises(ValueError):
            m.ratio([dict(pieces=1, garbage_lines=0)])

    def test_fixed_difference(self):
        b = [dict(pieces=250, garbage_lines=100)] * 15
        c = [dict(pieces=200, garbage_lines=100)] * 15
        self.assertEqual(m.bootstrap_difference(b, c, 100), [-.5, -.5])
        self.assertEqual(m.bootstrap_difference(c, b, 100), [.5, .5])
        self.assertEqual(m.bootstrap_difference(b, b, 100), [0, 0])
        with self.assertRaises(ValueError):
            m.bootstrap_difference(b[:1], c)

    def test_bootstrap_reproducible(self):
        b = [dict(pieces=n, garbage_lines=100) for n in (200, 240, 210)]
        c = [dict(pieces=n, garbage_lines=101) for n in (220, 245, 225)]
        self.assertEqual(m.bootstrap_difference(b, c, 300), m.bootstrap_difference(b, c, 300))

    def test_completion_and_uncertainty_before_efficiency(self):
        b = dict(trials=15, completed=15, pieces_per_garbage_line=2.5, diagnostic_fallbacks=0)
        c = dict(b, pieces_per_garbage_line=2.0)
        self.assertTrue(m.metric_gate(b, c, [-.7, -.1]))
        self.assertFalse(m.metric_gate(b, dict(c, completed=14), [-.7, -.1]))
        self.assertFalse(m.metric_gate(b, dict(c, trials=14), [-.7, -.1]))
        self.assertFalse(m.metric_gate(b, dict(c, diagnostic_fallbacks=1), [-.7, -.1]))
        self.assertFalse(m.metric_gate(b, c, [-.7, 0]))
        self.assertFalse(m.metric_gate(b, dict(c, pieces_per_garbage_line=2.49), [-.1, -.01]))


if __name__ == '__main__':
    unittest.main()
