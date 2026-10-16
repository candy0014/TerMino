#!/usr/bin/env python3
"""Bounded retrospective terrain fitting; protocol, registration and fit are separate.

No production integration or match execution. Registration validates all exported
roots and freezes input/code hashes without fitting. Fit requires the coordinator
to supply the exact approved registration hash. No adjustable training CLI knobs.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path


FEATURES = ("roof", "ColTrans", "RowTrans", "HoleLine", "WellDepth", "HoleDepth", "coverWeighted")
ORIGINAL_WEIGHTS = (128.0, 160.0, 160.0, 380.0, 100.0, 40.0, 1.0)
REGULARIZERS = (0.01, 0.1, 1.0)
STEPS = 2048
SCORE_UNIT = 560.0
BOX_BOUND = 0.5
EQUIVALENCE_TOLERANCE = 1e-7
MIN_ROOTS_PER_ROUND = 5
ROUND_ROOT_COUNTS = {"5han#2": 119, "5han#11": 18, "bennxt#6": 121,
                     "bennxt#13": 15, "icly2#1": 105, "mochengine2vszzz#9": 15}
CASE_ROUNDS = {"pressure_1": "5han#2", "pressure_2": "bennxt#6", "pressure_3": "icly2#1",
               "burst_1": "bennxt#13", "burst_2": "5han#11", "burst_3": "mochengine2vszzz#9"}


def protocol():
    return {
        "schema_version": 1, "status": "protocol_frozen_before_registration_and_fit",
        "scope": "Exploratory development on already observed deliberately strong Moch examples; not an external unseen set or a win-rate test",
        "enabled_in_production": False, "horizon": 5,
        "feature_names": list(FEATURES), "original_positive_penalties": list(ORIGINAL_WEIGHTS),
        "formula": "value = fixed_offset - dot(original_weights * (1 + delta), features)",
        "fixed_terms": "All nonterrain terms, including attack, B2B, combo, garbage clearing, danger, path risk, hold and time discounts; no intercept or extra feature",
        "inactive_upstream_term": "Botris HoleCount is never incremented in this revision; its zero term is not fitted",
        "delta_bounds": [-BOX_BOUND, BOX_BOUND], "score_unit": SCORE_UNIT,
        "objective": "round/root/negative balanced mean log(1 + exp(-(teacher_value - negative_value)/560)) + lambda/2 * dot(delta, delta)",
        "regularizers": list(REGULARIZERS), "iterations": STEPS,
        "optimizer": "Deterministic full-batch projected gradient from seven zeros; rate = 1 / (0.25 * sum(pair_mass * squared_norm(scaled_feature_difference)) + lambda); clip each delta to [-0.5,0.5] after every step; no early stopping",
        "normalization": "Only score differences divided by 560; no learned centering/scaling",
        "training_mass": "Equal source-round mass; equal eligible-root mass within each round; equal mass for every exported negative within each root",
        "validation": "Six leave-one-source-round-out folds; each lambda fitted on the other five complete rounds; choose smallest unregularized macro validation logistic loss, exact ties prefer larger lambda",
        "validation_gate": "Selected lambda's leave-one-round-out macro logistic loss must be strictly below the original delta=0 macro loss; otherwise report failure, skip final fit and export no model",
        "final_fit": "Only after passing the validation gate, fit all eligible roots exactly once at the selected lambda; do not choose by first-move similarity or in-sample loss",
        "expected_round_root_counts_including_failures": ROUND_ROOT_COUNTS,
        "case_rounds": CASE_ROUNDS, "expected_sample_ids": [0, 392],
        "minimum_eligible_roots_per_round": MIN_ROOTS_PER_ROUND,
        "negative_policy": "Use all 1..8 exported distinct first_root_key branches without further selection; same-teacher-first-move branches, equal scores and zero feature differences remain",
        "failure_policy": "Retain every failed/ineligible root and reason in the input and audit; any missing group, insufficient group, malformed record or linear-equivalence failure stops the whole batch; no extra roots, changed groups or automatic retries",
        "linear_equivalence_absolute_tolerance": EQUIVALENCE_TOLERANCE,
        "baseline_arithmetic": "If later integrated, preserve sequential per-hole-row cover subtraction at delta=0; this offline tool is not runtime equivalence certification",
        "pre_registration_amendment": {"requested_by": "root coordinator before valid dataset registration or fitting",
            "changes": ["Strict out-of-fold improvement gate before final fit/model export", "Formula absolute tolerance 1e-7"],
            "superseded_protocol_sha256": "ef9149057abd1c2ce365beab4316fceb9e999572867ce1c6cab0e852d93eb0c6"},
        "fit_authorization": "Separate coordinator approval after reviewing preregistration.json; exact registration SHA256 required by fit",
        "future_match_validation": "Any production candidate requires a separate new frozen match validation; this tool never launches games",
    }


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_new_json(path, value):
    with Path(path).open("x", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True, allow_nan=False)
        output.write("\n")


def finite(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{name}: expected finite number")
    return float(value)


def dot(left, right):
    return math.fsum(a * b for a, b in zip(left, right))


def check_final(final, label):
    features = final.get("features")
    if not isinstance(features, list) or len(features) != 7:
        raise ValueError(f"{label}: expected seven terrain features")
    features = [finite(value, f"{label}.{name}") for name, value in zip(FEATURES, features)]
    if any(value < 0 for value in features):
        raise ValueError(f"{label}: negative terrain feature")
    values = {key: finite(final.get(key), f"{label}.{key}") for key in
              ("fixed_offset", "original_value", "terrain_value", "terrain_dot", "terrain_residual")}
    expected = -dot(ORIGINAL_WEIGHTS, features)
    errors = [abs(values["terrain_value"] - expected), abs(values["terrain_dot"] - expected),
              abs(values["terrain_residual"]),
              abs(values["terrain_residual"] - (values["terrain_value"] - values["terrain_dot"])),
              abs(values["original_value"] - (values["fixed_offset"] + expected))]
    if not math.isfinite(expected) or max(errors) > EQUIVALENCE_TOLERANCE:
        raise ValueError(f"{label}: terrain linear equivalence failed, max absolute error={max(errors)}")
    return max(errors)


def validate_dataset(records):
    counts, eligible, failures = Counter(), Counter(), Counter()
    ids, failed_roots = set(), []
    maximum_error = 0.0
    pairs = tied = zero_feature_difference = same_first = 0
    for row in records:
        sample = row.get("sample_id")
        if type(sample) is not int or sample < 0 or sample in ids:
            raise ValueError(f"invalid or duplicate sample_id: {sample}")
        ids.add(sample)
        group = row.get("group")
        source, round_id = row.get("source"), row.get("round")
        if (not isinstance(source, str) or type(round_id) is not int or
                group != f"{Path(source).stem}#{round_id}" or
                CASE_ROUNDS.get(row.get("case_id")) != group):
            raise ValueError(f"sample {sample}: source-round grouping mismatch")
        if row.get("schema_version") != 1 or type(row.get("eligible")) is not bool:
            raise ValueError(f"sample {sample}: schema/eligible contract failed")
        reasons = row.get("reasons")
        if not isinstance(reasons, list) or any(not isinstance(reason, str) or not reason for reason in reasons):
            raise ValueError(f"sample {sample}: reasons must be a list of nonempty strings")
        counts[group] += 1
        if not row["eligible"]:
            if not reasons:
                raise ValueError(f"sample {sample}: failed root requires a reason")
            failures.update(reasons)
            failed_roots.append({key: row.get(key) for key in
                                 ("sample_id", "group", "reasons", "failed_depth", "teacher_prefix")})
            continue
        if reasons:
            raise ValueError(f"sample {sample}: eligible root has failure reasons")
        teacher, negatives = row.get("teacher"), row.get("negatives")
        if not isinstance(teacher, dict) or len(teacher.get("steps", [])) != 5:
            raise ValueError(f"sample {sample}: teacher must have exactly five steps")
        maximum_error = max(maximum_error, check_final(teacher.get("final", {}), f"sample {sample} teacher"))
        if not isinstance(negatives, list) or not 1 <= len(negatives) <= 8:
            raise ValueError(f"sample {sample}: expected 1..8 negatives")
        first_keys = set()
        for index, negative in enumerate(negatives):
            label = f"sample {sample} negative {index}"
            key = negative.get("first_root_key")
            if not isinstance(key, str) or not key or key in first_keys:
                raise ValueError(f"{label}: missing/duplicate first-root branch key")
            first_keys.add(key)
            if len(negative.get("steps", [])) != 5 or type(negative.get("same_teacher_first_move")) is not bool:
                raise ValueError(f"{label}: depth/first-move metadata invalid")
            final = negative.get("final", {})
            maximum_error = max(maximum_error, check_final(final, label))
            pairs += 1
            tied += final["original_value"] == teacher["final"]["original_value"]
            zero_feature_difference += final["features"] == teacher["final"]["features"]
            same_first += negative["same_teacher_first_move"]
        eligible[group] += 1
    if ids != set(range(393)) or dict(counts) != ROUND_ROOT_COUNTS:
        raise ValueError(f"expected every one of the frozen 393 roots and six original groups; got {dict(counts)}")
    insufficient = {group: eligible[group] for group in ROUND_ROOT_COUNTS if eligible[group] < MIN_ROOTS_PER_ROUND}
    if insufficient:
        raise ValueError(f"insufficient eligible roots (minimum {MIN_ROOTS_PER_ROUND}/round): {insufficient}; stop without adding data or regrouping")
    return {"total_roots": len(records), "eligible_roots": sum(eligible.values()),
            "all_roots_per_round": dict(counts), "eligible_roots_per_round": dict(eligible),
            "pairs": pairs, "exact_original_score_ties_retained": tied,
            "zero_feature_difference_pairs_retained": zero_feature_difference,
            "same_teacher_first_move_pairs_retained": same_first,
            "max_linear_equivalence_error": maximum_error,
            "failure_reasons": dict(failures), "failed_roots": failed_roots,
            "fit_performed": False, "first_move_ranking_reported": False}


def load_dataset(path):
    with Path(path).open(encoding="utf-8") as source:
        records = [json.loads(line) for line in source if line.strip()]
    audit = validate_dataset(records)
    return sorted((row for row in records if row["eligible"]), key=lambda row: row["sample_id"]), audit


def pair_terms(roots):
    rounds = Counter(root["group"] for root in roots)
    terms = []
    for root in sorted(roots, key=lambda row: row["sample_id"]):
        teacher = root["teacher"]["final"]
        mass = 1.0 / (len(rounds) * rounds[root["group"]] * len(root["negatives"]))
        for index, negative in enumerate(root["negatives"]):
            final = negative["final"]
            base = (teacher["original_value"] - final["original_value"]) / SCORE_UNIT
            difference = tuple(weight * (n - t) / SCORE_UNIT for weight, n, t in
                               zip(ORIGINAL_WEIGHTS, final["features"], teacher["features"]))
            if not all(math.isfinite(value) for value in (base, *difference)):
                raise ValueError("nonfinite scaled pair")
            terms.append({"sample_id": root["sample_id"], "group": root["group"],
                          "negative_index": index, "mass": mass, "base": base, "difference": difference})
    if not terms or not math.isclose(math.fsum(pair["mass"] for pair in terms), 1.0, abs_tol=1e-12):
        raise ValueError("empty comparisons or invalid balanced mass")
    return terms


def softplus_negative(margin):
    return max(0.0, -margin) + math.log1p(math.exp(-abs(margin)))


def sigmoid_negative(margin):
    if margin >= 0:
        value = math.exp(-margin)
        return value / (1.0 + value)
    return 1.0 / (1.0 + math.exp(margin))


def loss_and_gradient(terms, delta, regularizer=0.0):
    loss = regularizer * dot(delta, delta) / 2
    gradient = [regularizer * value for value in delta]
    for pair in terms:
        margin = pair["base"] + dot(pair["difference"], delta)
        loss += pair["mass"] * softplus_negative(margin)
        factor = -pair["mass"] * sigmoid_negative(margin)
        for j, value in enumerate(pair["difference"]):
            gradient[j] += factor * value
    return loss, gradient


def fit_coefficients(roots, regularizer):
    if regularizer not in REGULARIZERS:
        raise ValueError("regularizer is outside frozen grid")
    terms = pair_terms(roots)
    lipschitz = .25 * math.fsum(pair["mass"] * dot(pair["difference"], pair["difference"]) for pair in terms) + regularizer
    rate = 1.0 / lipschitz
    delta = [0.0] * 7
    for _ in range(STEPS):
        _, gradient = loss_and_gradient(terms, delta, regularizer)
        delta = [max(-BOX_BOUND, min(BOX_BOUND, value - rate * g)) for value, g in zip(delta, gradient)]
    objective, gradient = loss_and_gradient(terms, delta, regularizer)
    projected_residual = max(abs(value - max(-BOX_BOUND, min(BOX_BOUND, value - rate * g))) / rate
                             for value, g in zip(delta, gradient))
    return {"delta": delta, "positive_penalties": [weight * (1 + change) for weight, change in zip(ORIGINAL_WEIGHTS, delta)],
            "regularizer": regularizer, "iterations": STEPS, "step_size": rate,
            "objective": objective, "projected_gradient_inf": projected_residual,
            "roots": len(roots), "rounds": len({row["group"] for row in roots}), "pairs": len(terms)}


def evaluate(roots, delta):
    grouped = defaultdict(list)
    for row in roots:
        grouped[row["group"]].append(row)
    losses = {group: loss_and_gradient(pair_terms(rows), delta)[0] for group, rows in sorted(grouped.items())}
    return {"round_losses": losses, "macro_loss": math.fsum(losses.values()) / len(losses)}


def cross_validate_and_fit(roots):
    baseline = evaluate(roots, [0.0] * 7)
    results = []
    for regularizer in REGULARIZERS:
        folds = []
        for held_out in sorted(ROUND_ROOT_COUNTS):
            training = [row for row in roots if row["group"] != held_out]
            validation = [row for row in roots if row["group"] == held_out]
            model = fit_coefficients(training, regularizer)
            folds.append({"held_out_round": held_out, "training_rounds": sorted({row["group"] for row in training}),
                          "validation_roots": len(validation), "model": model,
                          "validation_loss": evaluate(validation, model["delta"])["macro_loss"]})
        results.append({"regularizer": regularizer, "folds": folds,
                        "macro_validation_loss": math.fsum(fold["validation_loss"] for fold in folds) / 6})
    chosen = min(results, key=lambda result: (result["macro_validation_loss"], -result["regularizer"]))
    passed = chosen["macro_validation_loss"] < baseline["macro_loss"]
    final_model = fit_coefficients(roots, chosen["regularizer"]) if passed else None
    return {"cross_validation": results, "selected_regularizer": chosen["regularizer"],
            "selected_macro_validation_loss": chosen["macro_validation_loss"],
            "validation_gate_passed": passed, "final_model": final_model, "baseline": baseline,
            "fitted_in_sample": evaluate(roots, final_model["delta"]) if passed else None,
            "interpretation": protocol()["scope"], "fit_count": 19 if passed else 18,
            "warning": "No first-move similarity selection or win-rate claim; all full-data scores are retrospective in-sample diagnostics"}


def register(data, output):
    output = Path(output)
    frozen = output / "protocol.json"
    if json.loads(frozen.read_text()) != protocol():
        raise ValueError("frozen protocol differs from fitter constants; do not silently replace it")
    try:
        roots, audit = load_dataset(data)
    except (ValueError, KeyError, TypeError) as error:
        write_new_json(output / "validation_failure.json", {"fit_performed": False, "error": str(error),
            "data_sha256": digest(data), "instruction": "Stop; no extra roots, regrouping or tolerance change"})
        raise
    write_new_json(output / "data_audit.json", audit)
    registration = {"schema_version": 1, "status": "registered_awaiting_root_approval_before_fit",
                    "data_path": str(Path(data).resolve()), "data_sha256": digest(data),
                    "protocol_sha256": digest(frozen), "trainer_sha256": digest(__file__),
                    "data_audit_sha256": digest(output / "data_audit.json"),
                    "eligible_sample_ids": [row["sample_id"] for row in roots], "fit_performed": False}
    write_new_json(output / "preregistration.json", registration)
    return {"fit_performed": False, "registration_sha256": digest(output / "preregistration.json"), "counts": audit}


def fit_registered(registration_path, approved_hash):
    path = Path(registration_path)
    if not approved_hash or digest(path) != approved_hash:
        raise ValueError("exact approved registration SHA256 required before fitting")
    registration = json.loads(path.read_text())
    output = path.parent
    if (registration["trainer_sha256"] != digest(__file__) or
            registration["protocol_sha256"] != digest(output / "protocol.json") or
            registration["data_audit_sha256"] != digest(output / "data_audit.json") or
            registration["data_sha256"] != digest(registration["data_path"]) or
            json.loads((output / "protocol.json").read_text()) != protocol()):
        raise ValueError("registered code, protocol, audit or dataset changed; stop without fitting")
    roots, audit = load_dataset(registration["data_path"])
    if [row["sample_id"] for row in roots] != registration["eligible_sample_ids"]:
        raise ValueError("eligible root set differs from registration")
    fit_output = output / "fit_artifacts"
    fit_output.mkdir()  # Refuse to replace or silently rerun an existing fit.
    report = cross_validate_and_fit(roots)
    report.update(registration_sha256=approved_hash, data_audit=audit)
    write_new_json(fit_output / "report.json", report)
    if report["validation_gate_passed"]:
        write_new_json(fit_output / "model.json", {**report["final_model"], "feature_names": FEATURES,
            "original_positive_penalties": ORIGINAL_WEIGHTS, "registration_sha256": approved_hash,
            "enabled_in_production": False, "scope": protocol()["scope"]})
    selected = next(result for result in report["cross_validation"] if result["regularizer"] == report["selected_regularizer"])
    out_of_fold_delta = {fold["held_out_round"]: fold["model"]["delta"] for fold in selected["folds"]}
    # Preserve every exported pair, including ties and terrain-inseparable ones.
    with (fit_output / "pair_predictions.jsonl").open("x", encoding="utf-8") as target:
        for pair in pair_terms(roots):
            target.write(json.dumps({**pair,
                "selected_out_of_fold_margin": pair["base"] + dot(pair["difference"], out_of_fold_delta[pair["group"]]),
                "final_fitted_margin": pair["base"] + dot(pair["difference"], report["final_model"]["delta"])
                    if report["validation_gate_passed"] else None}, allow_nan=False) + "\n")
    return {"output": str(fit_output), "selected_regularizer": report["selected_regularizer"],
            "validation_gate_passed": report["validation_gate_passed"], "fit_count": report["fit_count"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    prepare = actions.add_parser("protocol", help="Freeze settings before inspecting the exported dataset")
    prepare.add_argument("--output", type=Path, required=True)
    registration = actions.add_parser("register", help="Validate and hash data; never fit")
    registration.add_argument("--data", type=Path, required=True)
    registration.add_argument("--output", type=Path, required=True)
    fit = actions.add_parser("fit", help="Only after coordinator approval of the registered artifacts")
    fit.add_argument("--registration", type=Path, required=True)
    fit.add_argument("--approved-registration-sha256", required=True)
    args = parser.parse_args()
    if args.action == "protocol":
        args.output.mkdir(parents=True, exist_ok=True)
        write_new_json(args.output / "protocol.json", protocol())
        result = {"fit_performed": False, "protocol_sha256": digest(args.output / "protocol.json")}
    elif args.action == "register":
        result = register(args.data, args.output)
    else:
        result = fit_registered(args.registration, args.approved_registration_sha256)
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
