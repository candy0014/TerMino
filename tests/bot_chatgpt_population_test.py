import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('population_report', Path(__file__).resolve().parents[1] / 'src/bot/chatgpt/population_report.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class PopulationTest(unittest.TestCase):
    def test_wilson(self):
        self.assertIsNone(m.wilson(0, 0))
        self.assertAlmostEqual(m.wilson(0, 20)[1], 0.16112515805281938)
        lo, hi = m.wilson(10, 20)
        self.assertAlmostEqual(lo + hi, 1)
        self.assertAlmostEqual(m.wilson(20, 20)[0], 1-m.wilson(0, 20)[1])

    def test_series_not_piece_weighting(self):
        rows = [dict(wins=3, losses=0), dict(wins=0, losses=3)]
        self.assertEqual(m.fraction(rows), 0.5)
        self.assertEqual(m.fraction([dict(wins=3, losses=2), dict(wins=0, losses=3)]), 3/8)

    def test_bootstrap_extreme(self):
        b = [dict(wins=0, losses=3)]*4
        c = [dict(wins=3, losses=0)]*4
        self.assertEqual(m.bootstrap_difference(b,c,100), [1,1])
        self.assertEqual(m.bootstrap_difference(c,b,100), [-1,-1])
        self.assertEqual(m.bootstrap_difference(b,b,100), [0,0])
        self.assertIsNone(m.bootstrap_difference(b[:1],c,100))

    def test_bootstrap_reproducible(self):
        b = [dict(wins=0, losses=3),dict(wins=1,losses=3),dict(wins=3,losses=2)]
        c = [dict(wins=3, losses=1),dict(wins=0,losses=3),dict(wins=3,losses=0)]
        self.assertEqual(m.bootstrap_difference(b,c,500),m.bootstrap_difference(b,c,500))


if __name__ == '__main__':
    unittest.main()
