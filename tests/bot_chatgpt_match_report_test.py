#!/usr/bin/env python3
"""Guard statistical completion gates and exclusion of invalid match batches."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "src/bot/chatgpt/analysis/match_report.py"
SPEC = importlib.util.spec_from_file_location("match_report", SOURCE)
REPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPORT)


class MatchReportTest(unittest.TestCase):
    def setUp(self):
        self.storage = tempfile.TemporaryDirectory()
        self.root = Path(self.storage.name)

    def tearDown(self):
        self.storage.cleanup()

    def series(self, group, wins=3, losses=0, case="vs_4_turn1.0", phase="validation",
               games=None, draws=0):
        directory = self.root / str(group)
        directory.mkdir()
        rows = []
        for game in range(wins + losses + draws):
            for side in ("first", "second"):
                victory = (game < wins) == (side == "first")
                outcome = "draw" if game >= wins + losses else "won" if victory else "dead"
                rows.append(dict(case=case, seed=group * 100 + game, side=side,
                    outcome=outcome, bot=9 if side == "first" else 4,
                    width=300, pieces=100, attack=120, game_seconds=100,
                    diagnostic_decisions=0, diagnostic_fallbacks=0,
                    parameters=dict(turn_pieces=1, opponent_turn_pieces=float(case.split("turn")[1])),
                    clear_types={"T:spin:2": 20, "I:normal:4": 10, "J:normal:1": 10}))
        (directory / "results.jsonl").write_text("\n".join(json.dumps(r) for r in rows))
        score = dict(first=wins, second=losses)
        metadata = dict(simulation_pps=1, autoplay_pps=0,
                        versus_protocol="turn-pieces-v2", binary_sha256="frozen")
        job = dict(phase=phase, variant="candidate", case=case, group=group, output=str(directory))
        if games is not None:
            score.update(draws=draws, games=wins + losses + draws)
            metadata.update(versus_games=games, versus_first_to=None)
            job["games"] = games
        (directory / (case + "_score.json")).write_text(json.dumps(score))
        (directory / "metadata.json").write_text(json.dumps(metadata))
        return job

    def summary(self, jobs, case="vs_4_turn1.0", phase="validation", protocol="ft3"):
        return REPORT.report(jobs)["protocols"][protocol]["phases"][phase]["candidate"][case]

    def test_small_perfect_sample_is_incomplete(self):
        value = self.summary([self.series(1)])
        self.assertEqual(value["status"], "incomplete")
        self.assertEqual(value["win_fraction"], 1)
        self.assertEqual(self.summary([self.series(2)], case="vs_1_turn1.3")["status"], "incomplete")

    def test_complete_perfect_sample_reports_uncertainty(self):
        value = self.summary([self.series(i) for i in range(1, 6)])
        self.assertEqual(value["status"], "sample_target_met")
        self.assertLess(value["game_win_wilson_95"][0], 0.95)
        self.assertAlmostEqual(value["attack_per_piece"], 1.2)
        self.assertAlmostEqual(value["special_clear_per_piece"], 0.3)
        self.assertAlmostEqual(value["special_clear_fraction"], 0.75)

    def test_quota_threshold_is_strict_and_case_specific(self):
        jobs = [self.series(i, wins=3 if i <= 3 else 0, losses=0 if i <= 3 else 3,
                            case="vs_4_turn1.3") for i in range(1, 6)]
        value = self.summary(jobs, case="vs_4_turn1.3")
        self.assertEqual(value["status"], "sample_target_met")
        self.assertAlmostEqual(value["win_fraction"], 0.6)
        self.assertEqual(self.summary(jobs)["status"], "incomplete")

    def test_invalid_marker_excludes_records(self):
        jobs = [self.series(i) for i in range(1, 6)]
        (Path(jobs[0]["output"]) / "INVALID.json").write_text("{}")
        value = self.summary(jobs)
        self.assertEqual(value["status"], "incomplete")
        self.assertEqual(value["wins"], 12)
        self.assertEqual(value["complete_series"], 4)

    def test_duplicate_series_cannot_complete_validation(self):
        job = self.series(1)
        value = self.summary([job] * 5)
        self.assertEqual(value["status"], "incomplete")
        self.assertIn("series overlap in game seeds", value["issues"])

    def test_development_does_not_validate_target(self):
        jobs = [self.series(i, phase="dev") for i in range(1, 6)]
        self.assertEqual(self.summary(jobs, phase="dev")["status"], "development_only")

    def test_fixed_ten_games_need_one_complete_group(self):
        value = self.summary([self.series(1, wins=10, games=10)], protocol="fixed_10")
        self.assertEqual(value["status"], "sample_target_met")
        self.assertEqual(value["required_series"], 1)
        self.assertEqual(value["n"], 10)
        self.assertLess(value["game_win_wilson_95"][0], 0.95)
        self.assertEqual(value["target"]["required_wins_per_10_games"], 10)

    def test_fixed_development_reports_measured_not_validated_target(self):
        value = self.summary([self.series(1, wins=10, games=10, phase="developer")],
                             phase="developer", protocol="fixed_10")
        self.assertEqual(value["status"], "sample_target_met")
        self.assertFalse(value["independent_validation"])

    def test_fixed_partial_is_incomplete_even_if_all_games_won(self):
        value = self.summary([self.series(1, wins=9, games=10)], protocol="fixed_10")
        self.assertEqual(value["status"], "incomplete")
        self.assertIsNone(value["sample_target_met"])
        self.assertEqual(value["n"], 9)

    def test_fixed_draws_count_against_target(self):
        equal = self.summary([self.series(1, wins=9, draws=1, games=10)], protocol="fixed_10")
        self.assertEqual(equal["status"], "below_target")
        self.assertEqual(equal["win_fraction"], 0.9)
        self.assertEqual(equal["decisive_win_fraction"], 1)
        slow = self.summary([self.series(2, wins=5, draws=5, games=10, case="vs_4_turn1.3")],
                            case="vs_4_turn1.3", protocol="fixed_10")
        self.assertEqual(slow["status"], "below_target")
        self.assertEqual(slow["win_fraction"], 0.5)

    def test_fixed_six_wins_at_quota_1_3_meets_measured_target(self):
        value = self.summary([self.series(1, wins=6, losses=2, draws=2, games=10,
                                         case="vs_4_turn1.3")], case="vs_4_turn1.3", protocol="fixed_10")
        self.assertEqual(value["status"], "sample_target_met")
        self.assertEqual(value["n"], 10)
        self.assertEqual(value["target"]["required_wins_per_10_games"], 6)
        self.assertLess(value["game_win_wilson_95"][0], 0.5)

    def test_historical_ft3_never_enters_fixed_pool(self):
        fixed = self.series(1, wins=9, losses=1, games=10)
        historical = self.series(2)
        value = REPORT.report([fixed, historical])
        self.assertEqual(set(value["protocols"]), {"fixed_10", "ft3"})
        self.assertEqual(self.summary([fixed, historical], protocol="fixed_10")["n"], 10)
        self.assertEqual(self.summary([fixed, historical], protocol="ft3")["n"], 3)
        self.assertEqual(self.summary([fixed, historical], protocol="fixed_10")["status"], "below_target")

    def test_manifest_cannot_relabel_ft3_as_fixed(self):
        job = self.series(1)
        job["games"] = 10
        value = REPORT.report([job])
        self.assertEqual(set(value["protocols"]), {"ft3"})
        series = value["protocols"]["ft3"]["phases"]["validation"]["candidate"]["vs_4_turn1.0"]["series"][0]
        self.assertFalse(series["complete"])
        self.assertIn("stopping protocol differs from manifest", series["issues"])

    def test_manifest_shows_not_started_fixed_jobs(self):
        job = dict(phase="validation", variant="candidate", case="vs_4_turn1.3", group=1,
                   games=10, output=str(self.root / "pending"))
        value = self.summary([job], case="vs_4_turn1.3", protocol="fixed_10")
        self.assertEqual(value["status"], "incomplete")
        self.assertEqual(value["n"], 0)


if __name__ == "__main__":
    unittest.main()
