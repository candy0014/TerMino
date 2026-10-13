"""Synthetic-only regressions; never fit the observed Moch dataset."""
import copy
import importlib.util
import json
import math
import pathlib
import tempfile
import unittest
from collections import defaultdict
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("multi_step_fit", ROOT / "src/bot/chatgpt/analysis/moch_multi_step_fit.py")
fit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fit)


def terminal(features, offset=0.0):
    terrain = -fit.dot(fit.ORIGINAL_WEIGHTS, features)
    return dict(features=list(features), fixed_offset=offset, original_value=offset + terrain,
                terrain_value=terrain, terrain_dot=terrain, terrain_residual=0.0)


def root(sample, group, negative_count=1, inseparable=False):
    source, round_id = group.split("#")
    features = [2, 5, 4, 1, 3, 2, 16]
    negative_features = features if inseparable else [1, 3, 6, 1, 2, 3, 8]
    return dict(schema_version=1, sample_id=sample, case_id=next(k for k, v in fit.CASE_ROUNDS.items() if v == group),
                source=source + ".ttrm", round=int(round_id), placement=sample, group=group,
                eligible=True, reasons=[], teacher=dict(steps=[{} for _ in range(5)], final=terminal(features, 500)),
                negatives=[dict(trace_leaf=i, first_root_key=f"first-{i}", same_teacher_first_move=i == 0,
                                steps=[{} for _ in range(5)], final=terminal(negative_features, 500))
                           for i in range(negative_count)])


def full_dataset():
    records = []
    for group, count in fit.ROUND_ROOT_COUNTS.items():
        for _ in range(count):
            records.append(root(len(records), group, inseparable=True))
    return records


class MultiStepFitTest(unittest.TestCase):
    def test_round_root_and_negative_mass_are_each_balanced(self):
        groups = list(fit.ROUND_ROOT_COUNTS)
        roots = [root(0, groups[0], 1), root(1, groups[0], 3), root(2, groups[1], 8)]
        pairs = fit.pair_terms(roots)
        round_mass, root_mass = defaultdict(float), defaultdict(float)
        for pair in pairs:
            round_mass[pair["group"]] += pair["mass"]
            root_mass[pair["sample_id"]] += pair["mass"]
        self.assertEqual(set(round_mass), {groups[0], groups[1]})
        for mass in round_mass.values():
            self.assertAlmostEqual(mass, .5)
        self.assertEqual(dict(root_mass), {0: .25, 1: .25, 2: .5})
        self.assertEqual([p["mass"] for p in pairs if p["sample_id"] == 2], [.0625] * 8)

    def test_pair_margin_matches_fixed_offset_model_and_gradient(self):
        example = root(0, next(iter(fit.ROUND_ROOT_COUNTS)))
        pairs = fit.pair_terms([example])
        delta = [.1, -.2, .3, -.1, .2, -.3, .4]
        values = []
        for final in (example["teacher"]["final"], example["negatives"][0]["final"]):
            values.append(final["fixed_offset"] - fit.dot(
                [w * (1 + d) for w, d in zip(fit.ORIGINAL_WEIGHTS, delta)], final["features"]))
        actual = pairs[0]["base"] + fit.dot(pairs[0]["difference"], delta)
        self.assertAlmostEqual(actual, (values[0] - values[1]) / 560, places=12)
        _, gradient = fit.loss_and_gradient(pairs, delta, .1)
        epsilon = 1e-6
        for j in range(7):
            plus, minus = delta.copy(), delta.copy()
            plus[j] += epsilon
            minus[j] -= epsilon
            numerical = (fit.loss_and_gradient(pairs, plus, .1)[0] -
                         fit.loss_and_gradient(pairs, minus, .1)[0]) / (2 * epsilon)
            self.assertAlmostEqual(gradient[j], numerical, places=8)

    def test_optimizer_is_deterministic_bounded_and_keeps_fixed_rewards(self):
        roots = [root(0, next(iter(fit.ROUND_ROOT_COUNTS)))]
        original = copy.deepcopy(roots)
        before = fit.loss_and_gradient(fit.pair_terms(roots), [0.] * 7, .1)[0]
        first = fit.fit_coefficients(roots, .1)
        second = fit.fit_coefficients(roots, .1)
        self.assertEqual(first, second)
        self.assertEqual(first["iterations"], 2048)
        self.assertLess(first["objective"], before)
        self.assertTrue(all(-.5 <= value <= .5 for value in first["delta"]))
        self.assertTrue(all(.5 * old <= new <= 1.5 * old for old, new in
                            zip(fit.ORIGINAL_WEIGHTS, first["positive_penalties"])))
        self.assertEqual(roots, original)

    def test_ties_and_terrain_inseparable_comparisons_are_retained(self):
        group = next(iter(fit.ROUND_ROOT_COUNTS))
        roots = [root(0, group, 2, inseparable=True)]
        roots[0]["negatives"][1]["final"] = terminal(roots[0]["teacher"]["final"]["features"], 1060)
        terms = fit.pair_terms(roots)
        self.assertEqual(len(terms), 2)
        self.assertEqual([pair["base"] for pair in terms], [0.0, -1.0])
        self.assertTrue(all(pair["difference"] == (0.0,) * 7 for pair in terms))
        model = fit.fit_coefficients(roots, 1.0)
        self.assertEqual(model["delta"], [0.0] * 7)
        self.assertAlmostEqual(model["objective"], (math.log(2) + math.log1p(math.e)) / 2)
        self.assertEqual(fit.softplus_negative(1000), 0)
        self.assertEqual(fit.softplus_negative(-1000), 1000)

    def test_frozen_dataset_validation_keeps_all_failures_and_comparisons(self):
        data = full_dataset()
        by_group = defaultdict(int)
        for row in data:
            by_group[row["group"]] += 1
            if by_group[row["group"]] > 5:
                row.update(eligible=False, reasons=["no_valid_depth5_negative"], failed_depth=4, teacher_prefix=3)
        audit = fit.validate_dataset(data)
        self.assertEqual(audit["total_roots"], 393)
        self.assertEqual(audit["eligible_roots"], 30)
        self.assertEqual(audit["pairs"], 30)
        self.assertEqual(audit["exact_original_score_ties_retained"], 30)
        self.assertEqual(audit["zero_feature_difference_pairs_retained"], 30)
        self.assertEqual(audit["same_teacher_first_move_pairs_retained"], 30)
        self.assertEqual(len(audit["failed_roots"]), 363)
        data[0].update(eligible=False, reasons=["teacher_prefix_failure"])
        with self.assertRaisesRegex(ValueError, "insufficient eligible"):
            fit.validate_dataset(data)

    def test_equivalence_bad_groups_and_duplicate_branches_stop_whole_batch(self):
        valid = full_dataset()
        for change, pattern in (
                (lambda data: data[0]["teacher"]["final"].update(terrain_value=1), "linear equivalence"),
                (lambda data: data[0].update(group="invented-round"), "grouping mismatch"),
                (lambda data: data.pop(), "frozen 393"),
                (lambda data: data[0]["negatives"].append(copy.deepcopy(data[0]["negatives"][0])), "duplicate first-root")):
            data = copy.deepcopy(valid)
            change(data)
            with self.assertRaisesRegex(ValueError, pattern):
                fit.validate_dataset(data)

    def test_leave_one_round_out_uses_18_fits_then_one_final_fit(self):
        roots = [root(i, group) for i, group in enumerate(fit.ROUND_ROOT_COUNTS)]
        calls = []
        def fake_fit(rows, regularizer):
            calls.append(({row["group"] for row in rows}, regularizer))
            return {"delta": [-.5, -.5, .5, 0, -.5, .5, -.5]}
        with mock.patch.object(fit, "fit_coefficients", side_effect=fake_fit):
            report = fit.cross_validate_and_fit(roots)
        self.assertEqual(len(calls), 19)
        all_groups = set(fit.ROUND_ROOT_COUNTS)
        for result in report["cross_validation"]:
            for fold in result["folds"]:
                self.assertEqual(set(fold["training_rounds"]), all_groups - {fold["held_out_round"]})
                self.assertLess(fold["validation_loss"], report["baseline"]["macro_loss"])
        self.assertEqual(report["selected_regularizer"], 1.0)  # Exact ties prefer stronger L2.
        self.assertEqual(calls[-1], (all_groups, 1.0))
        self.assertTrue(report["validation_gate_passed"])

    def test_validation_tie_fails_and_prevents_full_data_fit(self):
        roots = [root(i, group, inseparable=True) for i, group in enumerate(fit.ROUND_ROOT_COUNTS)]
        with mock.patch.object(fit, "fit_coefficients", return_value={"delta": [0.0] * 7}) as optimizer:
            report = fit.cross_validate_and_fit(roots)
        self.assertEqual(optimizer.call_count, 18)
        self.assertEqual(report["selected_regularizer"], 1.0)
        self.assertFalse(report["validation_gate_passed"])
        self.assertIsNone(report["final_model"])
        self.assertIsNone(report["fitted_in_sample"])
        self.assertEqual(report["fit_count"], 18)

    def test_failed_validation_gate_writes_report_but_no_model(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary)
            data = output / "data.jsonl"
            data.write_text("".join(json.dumps(row) + "\n" for row in full_dataset()))
            fit.write_new_json(output / "protocol.json", fit.protocol())
            registered = fit.register(data, output)
            with mock.patch.object(fit, "fit_coefficients", return_value={"delta": [0.0] * 7}) as optimizer:
                result = fit.fit_registered(output / "preregistration.json", registered["registration_sha256"])
            self.assertFalse(result["validation_gate_passed"])
            self.assertEqual(optimizer.call_count, 18)
            self.assertFalse((output / "fit_artifacts/model.json").exists())
            self.assertTrue((output / "fit_artifacts/report.json").exists())
            pairs = [json.loads(line) for line in (output / "fit_artifacts/pair_predictions.jsonl").read_text().splitlines()]
            self.assertEqual(len(pairs), 393)
            self.assertTrue(all(pair["final_fitted_margin"] is None for pair in pairs))

    def test_registration_does_not_fit_and_changed_hash_prevents_fit(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary)
            data = output / "data.jsonl"
            data.write_text("".join(json.dumps(row) + "\n" for row in full_dataset()))
            fit.write_new_json(output / "protocol.json", fit.protocol())
            with mock.patch.object(fit, "fit_coefficients", side_effect=AssertionError("registration must not fit")):
                registered = fit.register(data, output)
            self.assertFalse(registered["fit_performed"])
            registration = output / "preregistration.json"
            with mock.patch.object(fit, "cross_validate_and_fit", side_effect=AssertionError("hash rejection must precede fitting")):
                with self.assertRaisesRegex(ValueError, "exact approved"):
                    fit.fit_registered(registration, "incorrect")
                data.write_text(data.read_text() + "\n")
                with self.assertRaisesRegex(ValueError, "dataset changed"):
                    fit.fit_registered(registration, registered["registration_sha256"])
            self.assertFalse((output / "fit_artifacts").exists())


if __name__ == "__main__":
    unittest.main()
