"""Stdlib-only regression tests for the main-executable benchmark's statistics."""
import importlib.util
import contextlib
import io
import json
import pathlib
import tempfile
import types
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("benchmark", ROOT / "src/bot/chatgpt/benchmark.py")
benchmark = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(benchmark)
REPORT_SPEC = importlib.util.spec_from_file_location("profile_report", ROOT / "src/bot/chatgpt/profile_report.py")
profile_report = importlib.util.module_from_spec(REPORT_SPEC)
REPORT_SPEC.loader.exec_module(profile_report)
CHAIN_SPEC = importlib.util.spec_from_file_location("chain_report", ROOT / "src/bot/chatgpt/chain_report.py")
chain_report = importlib.util.module_from_spec(CHAIN_SPEC)
CHAIN_SPEC.loader.exec_module(chain_report)
SEARCH_SPEC = importlib.util.spec_from_file_location("search_report", ROOT / "src/bot/chatgpt/search_report.py")
search_report = importlib.util.module_from_spec(SEARCH_SPEC)
SEARCH_SPEC.loader.exec_module(search_report)
ROOT_SPEC = importlib.util.spec_from_file_location("root_compare", ROOT / "src/bot/chatgpt/root_compare.py")
root_compare = importlib.util.module_from_spec(ROOT_SPEC)
ROOT_SPEC.loader.exec_module(root_compare)


def event(t, lines=0, attack=0, **values):
    result = dict(t=t, lines=lines, attack=attack, piece="Ｔ", spin=0, mini=0,
                  b2b_before=0, b2b_after=0, b2b_charge=0, garbage_lines=0)
    result.update(values)
    return {key: str(value) for key, value in result.items()}


def versus_records(case, seed, outcomes):
    completion = benchmark.versus_completion(
        {side: dict(outcome=outcome) for side, outcome in zip(("first", "second"), outcomes)}, "fixture")
    return [dict(case=case["name"], parameters=case, seed=seed, side=side,
                 bot=9 if side == "first" else case["opponent"], width=300,
                 wall_seconds=1, wall_pps=1, **completion,
                 **benchmark.summarize([event(0)], dict(outcome=outcome)))
            for side, outcome in zip(("first", "second"), outcomes)]


def versus_log(outcomes):
    placements = "".join("TETR_TEST_PLACEMENT side=" + side + " " + " ".join(
        f"{key}={value}" for key, value in event(0).items()) + "\n" for side in ("first", "second"))
    return placements + "TETR_LOCAL_VERSUS_RESULT " + " ".join(
        f"{side}_outcome={outcome} {side}_pieces=1 {side}_attack=0"
        for side, outcome in zip(("first", "second"), outcomes)) + "\n"


class BenchmarkTest(unittest.TestCase):
    def test_completed_game_protocol_counts_natural_draws_but_not_caps(self):
        case = benchmark.cases("versus")[0]
        sequence = [("goal", "opponent_left"), ("won", "dead")] * 5 + [("dead", "dead")] * 4
        records = []
        played = completed = 0
        while benchmark.continue_case(case, played, dict(first=5), 3,
                                      completed_versus_games=9, completed=completed):
            game = versus_records(case, 100 + played, sequence[played])
            completed += game[0]["completed"]
            records.extend(game)
            played += 1
        score = benchmark.versus_score(records, 9)
        self.assertEqual((played, completed), (14, 9))
        self.assertEqual((score["completed_first_wins"], score["completed_draws"], score["truncated"]), (5, 4, 5))
        self.assertEqual(score["required_first_wins"], 5)
        self.assertTrue(score["acceptance_passed"])
        self.assertEqual(score["seeds"], list(range(100, 114)))
        # An existing result set with omitted seeds must not pass acceptance.
        records[-2]["seed"] += 1
        self.assertFalse(benchmark.versus_score(records, 9)["acceptance_passed"])

    def test_completed_game_safety_cap_and_five_wins_never_stop_early(self):
        case = benchmark.cases("versus")[0]
        self.assertTrue(benchmark.continue_case(case, 5, dict(first=5), 3,
                                               completed_versus_games=9, completed=5))
        self.assertFalse(benchmark.continue_case(case, 12, {}, 3,
                                                completed_versus_games=9, completed=8,
                                                max_versus_games=12))
        records = [record for seed in range(12)
                   for record in versus_records(case, seed, ("goal", "opponent_left"))]
        score = benchmark.versus_score(records, 9, 12)
        self.assertTrue(score["safety_limit_reached"])
        self.assertFalse(score["completed_target_reached"])
        self.assertFalse(score["acceptance_passed"])
        self.assertIsNone(score["completed_first_win_rate"])
        records = [record for seed in range(9)
                   for record in versus_records(case, seed, ("won", "dead") if seed < 4 else ("dead", "won"))]
        self.assertFalse(benchmark.versus_score(records, 9)["acceptance_passed"])

    def test_versus_endings_are_explicit_and_do_not_guess_from_piece_count(self):
        case = benchmark.cases("versus")[0]
        args = types.SimpleNamespace(bot=9, width=300, pieces=1)
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            raw = directory / f"{case['name']}_seed1.log"
            for outcomes, completed in ((("won", "dead"), True), (("dead", "won"), True),
                                        (("dead", "dead"), True), (("goal", "opponent_left"), False)):
                raw.write_text(versus_log(outcomes))
                records = benchmark.parse_run(args, case, 1, directory, 1)
                self.assertEqual([r["completed"] for r in records], [completed, completed])
                self.assertEqual([r["truncated"] for r in records], [not completed, not completed])
                self.assertEqual(records[0]["termination_reason"], "natural" if completed else "piece_cap")
            for outcomes in (("opponent_left", "dead"), ("unknown", "dead"), ("won", "won"),
                             ("goal", "unknown"), ("dead", "goal")):
                raw.write_text(versus_log(outcomes))
                with self.assertRaisesRegex(RuntimeError, "unrecognized versus ending"):
                    benchmark.parse_run(args, case, 1, directory, 1)

    def test_reparse_rebuilds_completion_and_scores_using_original_protocol(self):
        case = benchmark.cases("versus")[0]
        outcomes = [("goal", "opponent_left")] + [("won", "dead")] * 5 + [("dead", "won")] * 4
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            previous = []
            for seed, ending in enumerate(outcomes, 100):
                (directory / f"{case['name']}_seed{seed}.log").write_text(versus_log(ending))
                previous.extend(versus_records(case, seed, ending))
            # The old JSONL had no completion classification; logs are authoritative.
            for record in previous:
                del record["completed"], record["truncated"], record["termination_reason"]
            (directory / "results.jsonl").write_text("".join(json.dumps(r) + "\n" for r in previous))
            (directory / "metadata.json").write_text(json.dumps(dict(
                versus_games=None, versus_first_to=None, versus_completed_games=9, versus_max_games=100)))
            with mock.patch.object(benchmark.os.sys, "argv", ["benchmark.py", "--output", str(directory), "--reparse"]), \
                    mock.patch.object(benchmark, "run_one") as child:
                benchmark.main()
                child.assert_not_called()
            score = json.loads((directory / f"{case['name']}_score.json").read_text())
            self.assertEqual((score["games"], score["completed"], score["truncated"]), (10, 9, 1))
            self.assertTrue(score["acceptance_passed"])
            self.assertEqual(score["completed_target"], 9)
            self.assertIn("termination_reason", (directory / "summary.csv").read_text().splitlines()[0])

    def test_completed_cli_runs_consecutive_seeds_and_keeps_safety_cap_results(self):
        case = benchmark.cases("versus")[0]
        for endings, maximum, expected_error in (
                ([("goal", "opponent_left")] + [("won", "dead")] * 9, 20, False),
                ([("goal", "opponent_left")] * 10, 10, True)):
            with self.subTest(safety_cap=expected_error), tempfile.TemporaryDirectory() as directory:
                directory = pathlib.Path(directory)
                executable = directory / "unused_binary"
                executable.write_bytes(b"mock binary; never launched")
                output = directory / "results"
                seeds = []
                def child(args, requested_case, seed, unused):
                    seeds.append(seed)
                    return versus_records(requested_case, seed, endings[len(seeds) - 1])
                argv = ["benchmark.py", "--output", str(output), "--binary", str(executable),
                        "--suite", "versus", "--case", case["name"], "--seed", "50",
                        "--completed-versus-games", "9", "--max-versus-games", str(maximum)]
                with mock.patch.object(benchmark.os.sys, "argv", argv), \
                        mock.patch.object(benchmark, "run_one", side_effect=child), \
                        mock.patch.object(benchmark.subprocess, "check_output", return_value="fixture-head"), \
                        contextlib.redirect_stdout(io.StringIO()):
                    if expected_error:
                        with self.assertRaisesRegex(RuntimeError, "safety cap"):
                            benchmark.main()
                    else:
                        benchmark.main()
                self.assertEqual(seeds, list(range(50, 60)))
                score = json.loads((output / f"{case['name']}_score.json").read_text())
                self.assertEqual(score["safety_limit_reached"], expected_error)
                self.assertEqual(score["acceptance_passed"], not expected_error)
                self.assertEqual(len((output / "results.jsonl").read_text().splitlines()), 20)
                self.assertTrue((output / "summary.csv").exists())

    def test_fixed_ten_games_do_not_stop_at_three_wins_and_count_draws(self):
        versus = benchmark.cases("versus")[0]
        for results in (["first"] * 12, ["second"] * 12,
                        ["first", "second"] * 6, [None] * 12):
            wins = dict(first=0, second=0)
            played = 0
            while benchmark.continue_case(versus, played, wins, 3):
                if results[played] is not None:
                    wins[results[played]] += 1
                played += 1
            self.assertEqual(played, 10)
        self.assertFalse(benchmark.continue_case(benchmark.cases("zen")[0], 3, {}, 3))

    def test_historical_win_based_stopping_requires_explicit_request(self):
        versus = benchmark.cases("versus")[0]
        self.assertTrue(benchmark.continue_case(versus, 3, dict(first=3, second=0), 3))
        self.assertFalse(benchmark.continue_case(versus, 3, dict(first=3, second=0),
                                                 3, first_to=3))
        self.assertTrue(benchmark.continue_case(versus, 4, dict(first=2, second=2),
                                                3, first_to=3))

    def test_fixed_root_pairs_require_matching_inputs_and_legal_moves(self):
        a = dict(line=1, pieces=0, weight=0, legal=True, selected=5, row=18,
                 col=4, rotation=0, spin=0, mini=0, holes_after=2, roof_after=5, projected_dead=0)
        b = dict(a, weight=80, row=19, holes_after=0, roof_after=4)
        summary = root_compare.summarize([a, b])
        self.assertEqual((summary['roots'], summary['changed']), (1, 1))
        self.assertEqual(summary['mean_holes_after'], [2, 0])
        for rows in ([a], [a, dict(b, pieces=1)], [a, dict(b, legal=False)], [b, a]):
            with self.assertRaises(ValueError):
                root_compare.summarize(rows)

    def test_search_work_report_is_not_a_score(self):
        row = dict(evaluated_depths=','.join(map(str, range(16))),
                   search_calls='10', cache_hits='3', visited='70', landings='25', selected_depth='7')
        report = search_report.summarize([row, row])
        self.assertEqual(report['decisions'], 2)
        self.assertEqual(report['cache_hit_fraction'], .3)
        self.assertEqual(report['mean_evaluated_by_depth'], list(range(16)))
        self.assertEqual(report['selected_depth_histogram'], {7: 2})
        self.assertIsNone(search_report.summarize([]))
        self.assertIsNone(search_report.summarize([dict(search_ms='100')]))
        with self.assertRaises(ValueError):
            search_report.summarize([dict(row, cache_hits='11')])
        with self.assertRaises(ValueError):
            search_report.summarize([dict(row, evaluated_depths='1,2')])

    def test_b2b_retention_preservation_and_censoring(self):
        events = [event(0, 1, spin=1, b2b_after=1),
                  event(1, b2b_before=1, b2b_after=1),
                  event(2, 1, spin=1, b2b_before=1, b2b_after=2),
                  event(3, 1, b2b_before=2),
                  event(4, 4, b2b_after=1)]
        r = chain_report.chain_metrics(events)
        self.assertEqual((r['started'], r['extended'], r['broken']), (2, 1, 1))
        self.assertEqual(r['incrementing_clears'], 3)
        # A normal PC can increment B2B without being a Spin/Quad.
        self.assertEqual(chain_report.chain_metrics([event(0, 2, b2b_after=1)])['incrementing_clears'], 1)
        self.assertEqual(r['active_clear_retention'], .5)
        self.assertEqual(r['completed_chain_lengths'], {2: 1})
        self.assertEqual(r['preserved_without_clear'], 1)
        self.assertEqual((r['final_active_chain'], r['peak_b2b']), (1, 2))
        total = chain_report.summarize([dict(chain=r, outcome='dead', pieces=5)])
        self.assertEqual(total['censored_final_chains'], {1: 1})
        self.assertIsNone(chain_report.chain_metrics([])['active_clear_retention'])

    def test_b2b_report_rejects_truncation_and_invalid_transition(self):
        for events in ([event(0, b2b_before=3, b2b_after=3)],
                       [event(0, b2b_after=1)],
                       [event(0, 1, b2b_after=-1)],
                       [event(0, 1, b2b_after=1), event(1, 1, b2b_before=1, b2b_after=2),
                        event(2, b2b_before=2, b2b_after=1)],
                       [event(0, 1, b2b_after=1), event(1, b2b_before=1)]):
            with self.assertRaises(ValueError):
                chain_report.chain_metrics(events)

    def test_secondary_band_boundaries_and_empty_histogram(self):
        report = profile_report.describe_histogram({"0": 1, "4": 2, "5": 3, "11": 1, "12": 2}, 4, 12)
        self.assertEqual((report["small"], report["middle"], report["large"]), (3, 4, 2))
        self.assertEqual(sum(report["fractions"].values()), 1)
        self.assertEqual(profile_report.describe_histogram({}, 4, 12)["events"], 0)
        with self.assertRaises(ValueError):
            profile_report.describe_histogram({"4": -1}, 4, 12)

    def test_report_keeps_primary_metrics_separate_from_tails(self):
        base = dict(case="example", side="first", bot=9, width=300, seed=1,
                    outcome="dead", pieces=100, attack=50, garbage_lines=20,
                    **{name: {"1": 2} for name in profile_report.BANDS})
        second = dict(base, seed=2, pieces=300, attack=300, garbage_lines=30)
        report = profile_report.summarize([base, second])[0]
        self.assertEqual(report["primary"]["mean_pieces"], 200)
        self.assertEqual(report["primary"]["attack_per_piece"], 350 / 400)
        self.assertEqual(report["primary"]["pieces_per_garbage_line"], 8)
        self.assertEqual(report["primary"]["outcomes"], {"dead": 2})
        self.assertEqual(report["secondary"]["spikes"]["events"], 4)

    def test_matrix(self):
        self.assertEqual(len(benchmark.cases("solo")), 19)
        self.assertEqual(len(benchmark.cases("versus")), 8)
        full = benchmark.cases("full")
        self.assertEqual(len(full), 27)
        self.assertEqual(len({case["name"] for case in full}), 27)
        for case in benchmark.cases("versus"):
            self.assertEqual(case["turn_pieces"], 1)
            self.assertIn(case["opponent_turn_pieces"], (0.9, 1, 1.15, 1.3))
            self.assertNotIn("opponent_pps", case)

    def test_solo_clock_and_no_turn_quota(self):
        settings = benchmark.config(benchmark.cases("zen")[0], 9, 300)
        self.assertIn("Simulation PPS = 1\n", settings)
        self.assertIn("Turn Pieces = 0\n", settings)

    def test_distributions_and_small_spikes(self):
        events = [event(0, 2, 5, spin=1, b2b_after=7),
                  event(1, 1, 6, b2b_before=7, b2b_charge=6),
                  event(2),
                  event(3, 1, 1, piece="Ｊ", spin=1, mini=1, b2b_after=1),
                  event(4, 1, 0, b2b_before=1, b2b_charge=0)]
        summary = benchmark.summarize(events, dict(b2b=3))
        self.assertEqual(summary["spikes"], {11: 1, 1: 1})
        self.assertEqual(summary["combo_lengths"], {2: 2})
        self.assertEqual(summary["combo_attacks"], {11: 1, 1: 1})
        self.assertEqual(summary["b2b_releases_including_final"], {6: 1, 0: 1, 3: 1})
        self.assertEqual(summary["clear_types"], {"T:spin:2": 1, "T:normal:1": 2, "J:minispin:1": 1})
        self.assertEqual(summary["special_clear_fraction"], 0.5)
        self.assertEqual(summary["attack_plus_b2b_per_piece"], 3)
        self.assertIsNone(summary["pieces_per_garbage_line"])

    def test_parse_and_reject_truncated_placements(self):
        case = benchmark.cases("zen")[0]
        args = types.SimpleNamespace(bot=9, width=300)
        placement = "TETR_TEST_PLACEMENT side=first " + " ".join(
            f"{key}={value}" for key, value in event(0, 1, 2, spin=1).items()) + "\n"
        result = "TETR_TEST_RESULT side=first outcome=goal pieces=1 attack=2 duration=1\n"
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            raw = directory / "zen_seed1.log"
            raw.write_text(placement + result, encoding="utf-8")
            records = benchmark.parse_run(args, case, 1, directory, 0.1)
            self.assertEqual(records[0]["wall_pps"], 10)
            self.assertEqual(records[0]["game_seconds"], 1)
            raw.write_text("TETR_BOT_DIAGNOSTIC side=first roof=12 fallback=3\n" + placement + result, encoding="utf-8")
            records = benchmark.parse_run(args, case, 1, directory, 0.1)
            self.assertEqual(records[0]["diagnostic_fallbacks"], 1)
            self.assertEqual(records[0]["diagnostic_peak_roof"], 12)
            self.assertTrue((directory / "zen_seed1.decisions.json").exists())
            raw.write_text(placement + result.replace("pieces=1", "pieces=2"), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "incomplete placement"):
                benchmark.parse_run(args, case, 1, directory, 0.1)

    def test_versus_batch_flags_and_isolated_settings(self):
        case = benchmark.cases("versus")[0]
        args = types.SimpleNamespace(bot=9, width=300, pieces=1000,
                                     binary=ROOT / "build/tetr", timeout=900)
        def child(*unused, **kwargs):
            env = kwargs["env"]
            for flag in ("TETR_TEST_BATCH", "TETR_LOCAL_VERSUS_BATCH",
                         "TETR_TEST_MAX_PIECES_FIRST_ONLY", "TETR_EXIT_AFTER_GAME"):
                self.assertEqual(env[flag], "1")
            self.assertNotEqual(pathlib.Path(kwargs["cwd"]), ROOT)
            first = pathlib.Path(env["TETR_USER_CONFIG"]).read_text()
            second = pathlib.Path(env["TETR_USER_CONFIG_PLAYER2"]).read_text()
            self.assertIn("Autoplay = 9\n", first)
            self.assertIn("Simulation PPS = 1\n", first)
            self.assertIn("Simulation PPS = 1\n", second)
            self.assertIn("Turn Pieces = 1\n", first)
            self.assertIn("Turn Pieces = 0.9\n", second)
            return types.SimpleNamespace(returncode=0)
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(benchmark.subprocess, "run", side_effect=child), \
                mock.patch.object(benchmark, "parse_run", return_value=[]) as parse:
            benchmark.run_one(args, case, 123, pathlib.Path(directory))
            parse.assert_called_once()

    def test_native_replay_preserved_before_temporary_directory_cleanup(self):
        args = types.SimpleNamespace(bot=9, width=300, pieces=10, save_replays=True,
                                     binary=ROOT / "build/tetr", timeout=900)
        for case in (benchmark.cases("zen")[0], benchmark.cases("versus")[0]):
            def child(*unused, **kwargs):
                flag = "TETR_LOCAL_VERSUS_REPLAY" if case["mode"] == 6 else "TETR_TEST_REPLAY"
                self.assertEqual(kwargs["env"][flag], "chatgpt_capture.txt")
                replay = pathlib.Path(kwargs["cwd"]) / "replay/chatgpt_capture.txt"
                replay.parent.mkdir()
                replay.write_text("native replay fixture", encoding="utf-8")
                return types.SimpleNamespace(returncode=0)
            with tempfile.TemporaryDirectory() as directory, \
                    mock.patch.object(benchmark.subprocess, "run", side_effect=child), \
                    mock.patch.object(benchmark, "parse_run", return_value=[]):
                benchmark.run_one(args, case, 123, pathlib.Path(directory))
                saved = pathlib.Path(directory) / f"{case['name']}_seed123.replay.txt"
                self.assertEqual(saved.read_text(), "native replay fixture")


if __name__ == "__main__":
    unittest.main()
