#!/usr/bin/env python3
"""Offline local-pattern data and bounded trainer; fitting is a separate command.

Preparation uses reviewed observations and the original pure-placement filter.
Audit uses the frozen native candidate exporter. Neither command fits weights or
reports teacher-choice ranks. Fit requires the exact frozen plan hash explicitly.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys

from moch_imitation import (
    ORACLE_SHA, Rejected, digest, integer, load_candidates, new_output,
    numerical_backend, prepare_root, root_line, selected_negatives, write_json,
)

SCORE_UNIT = 560.0
BOX_BOUND = 1.0 / 7.0
L1_BOUND = 2.0
STEPS = 200
REGULARIZERS = (0.01, 0.1, 1.0)
OFFSETS = ((-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1))
MIRROR_BITS = (2, 1, 0, 4, 3, 7, 6, 5)
FILE_GROUPS = {
    "5han.ttrm": "5han", "bennxt.ttrm": "bennxt",
    "icly.ttrm": "icly", "icly2.ttrm": "icly",
    "lovekokomi.ttrm": "lovekokomi", "mochengine2vszzz.ttrm": "zzz",
    "naraa0327.ttrm": "naraa0327", "naraa0327_2.ttrm": "naraa0327",
    "permafrostnewbie.ttrm": "permafrostnewbie", "talothebot.ttrm": "talothebot",
    "torawo.ttrm": "torawo", "turtle.ttrm": "turtle",
}
EXTERNAL_GROUPS = ("bennxt", "torawo")


def mirror(code):
    return sum(((code >> source) & 1) << target for target, source in enumerate(MIRROR_BITS))


CANONICAL_CODES = sorted({min(code, mirror(code)) for code in range(256)})
assert len(CANONICAL_CODES) == 144 and CANONICAL_CODES[0] == 0
CLASS_BY_CODE = [CANONICAL_CODES.index(min(code, mirror(code))) for code in range(256)]
PATTERN_DEFINITION = {
    "name": "empty-center-3x3-surface-counts", "version": 1, "dimensions": 144,
    "coordinate_system": "x right, y up, row masks bottom to top",
    "neighbor_offsets_by_bit": [list(pair) for pair in OFFSETS],
    "mirror_source_bit_by_output_bit": list(MIRROR_BITS),
    "canonical_codes_by_class": CANONICAL_CODES,
    "canonicalization": "min(code, horizontal_mirror(code)); class indices sorted by canonical code",
    "centers": "For each x, y=max(0,top[x]-3)..min(height-1,top[x]+1) inclusive; skip occupied centers",
    "top": "Highest occupied row plus one, or zero for an empty column",
    "boundaries": "All out-of-board neighbors, including top, are occupied",
    "zero_class": "All-empty code/class zero is not counted and its coefficient is fixed to zero",
    "scaling": "Raw integer counts, no centering or normalization",
}


def pattern_counts(rows):
    if len(rows) != 40 or any(type(row) is not int or not 0 <= row <= 1023 for row in rows):
        raise ValueError("Expected forty ten-bit bottom-origin rows")
    top = [0] * 10
    for y, row in enumerate(rows):
        for x in range(10):
            if row & (1 << x):
                top[x] = y + 1
    counts = [0] * 144
    for x in range(10):
        for y in range(max(0, top[x] - 3), min(39, top[x] + 1) + 1):
            if rows[y] & (1 << x):
                continue
            code = 0
            for bit, (dx, dy) in enumerate(OFFSETS):
                nx, ny = x + dx, y + dy
                if not (0 <= nx < 10 and 0 <= ny < 40) or rows[ny] & (1 << nx):
                    code |= 1 << bit
            if code:
                counts[CLASS_BY_CODE[code]] += 1
    return counts


def prepare(args):
    rounds, sources = [], []
    for source in sorted(args.sources, key=lambda p: str(p.resolve())):
        data = json.loads(source.read_text())
        if data.get("reviewedSha256") != ORACLE_SHA:
            raise ValueError("Unreviewed official-engine export")
        sources.append({"path": str(source.resolve()), "sha256": digest(source)})
        rounds.extend(record for record in data["rounds"] if record["username"] == "mochbot-dev")
    rounds.sort(key=lambda r: (r["file"], r["round"], r["side"]))
    seen, counts, samples, roots = set(), Counter(), [], []
    root_counts = Counter()
    for record in rounds:
        if record["file"] not in FILE_GROUPS:
            raise ValueError("No preregistered opponent group for " + record["file"])
        group = f"{record['file']}:{record['round']}:mochbot-dev"
        if group in seen:
            raise ValueError("Duplicate round " + group)
        seen.add(group)
        if "matched" not in record.get("validation", ""):
            raise ValueError("Source round lacks official end-stat validation")
        opponent = FILE_GROUPS[record["file"]]
        for index, placement in enumerate(record["placements"]):
            counts["teacher_observations"] += 1
            try:
                root = prepare_root(record, placement, record["placements"][index - 1] if index else None, 15)
            except Rejected as error:
                counts["reject:" + str(error)] += 1
                continue
            sample_id = len(samples)
            placement_index = integer(placement["index"], "placement index", 1, 1000000)
            roots.append(root_line(sample_id, placement_index, root))
            state_key = {k: root[k] for k in ("piece", "board", "b2b", "combo", "garbage_height", "previous_cleared")}
            samples.append({
                "sample_id": sample_id, "group": group, "opponent_group": opponent,
                "source": record["file"], "round": record["round"], "placement": placement_index,
                "state_sha256": hashlib.sha256(json.dumps(state_key, sort_keys=True).encode()).hexdigest(),
                "root_line_sha256": hashlib.sha256(roots[-1].encode()).hexdigest(),
                "scope": "conditional_selected_family", "visible_next_count": 5,
                "observed_hold_or_family_change": root["observed_hold_or_family_change"],
                "post_hold_visible_current_and_next": root["post_hold_visible_current_and_next"],
                "prior_spawn_visible_current_and_next": root["prior_spawn_visible_current_and_next"],
            })
            root_counts[opponent] += 1
    if len(seen) != 106 or counts["teacher_observations"] != 13784:
        raise ValueError("This preregistered corpus requires exactly 106 validated Moch rounds / 13,784 observations")
    if set(root_counts) != set(FILE_GROUPS.values()):
        raise ValueError("Missing preregistered opponent group")
    loads = [0, 0, 0]
    assignments = {}
    development_groups = sorted((g for g in root_counts if g not in EXTERNAL_GROUPS),
                                key=lambda g: (-root_counts[g], g))
    for group in development_groups:
        fold = min(range(3), key=lambda n: (loads[n], n))
        assignments[group] = fold
        loads[fold] += root_counts[group]
    for sample in samples:
        external = sample["opponent_group"] in EXTERNAL_GROUPS
        sample["split"] = "external" if external else "development"
        sample["fold"] = None if external else assignments[sample["opponent_group"]]
        counts["eligible:" + sample["split"]] += 1
    new_output(args.output)
    (args.output / "roots.log").write_text("\n".join(roots) + "\n")
    write_json(args.output / "manifest.json", {
        "schema_version": 1, "scope": "conditional_selected_family", "teacher": "mochbot-dev",
        "source_exports": sources, "oracle_sha256": ORACLE_SHA,
        "roots_sha256": digest(args.output / "roots.log"), "max_roof": 15,
        "queue_policy": "Current plus five retained for audit only; native input has one selected family, no hold or future",
        "counts": dict(counts), "verified_rounds": len(seen), "samples": samples,
        "file_to_opponent_group": FILE_GROUPS, "external_opponent_groups": EXTERNAL_GROUPS,
        "development_fold_assignment": assignments, "pre_native_group_root_counts": dict(root_counts),
        "pre_native_fold_root_counts": loads,
        "fold_policy": "Before native export, sort development groups by descending prefilter root count then name; assign to smallest root-count fold then smallest fold index",
        "pattern_definition": PATTERN_DEFINITION, "prepare_tool_sha256": digest(Path(__file__)),
        "original_filter_tool_sha256": digest(Path(__file__).with_name("moch_imitation.py")),
    })
    print(json.dumps({"counts": dict(counts), "folds": assignments, "fold_root_counts": loads}, indent=2))


def load_pattern_candidates(manifest_path, candidate_path):
    manifest, dataset, native_definition, counts = load_candidates(manifest_path, candidate_path)
    if manifest["pattern_definition"] != PATTERN_DEFINITION or manifest["file_to_opponent_group"] != FILE_GROUPS:
        raise ValueError("Pattern definition or file groups changed")
    for root in dataset:
        opponent = root["opponent_group"]
        expected_split = "external" if opponent in EXTERNAL_GROUPS else "development"
        expected_fold = None if expected_split == "external" else manifest["development_fold_assignment"][opponent]
        if root["split"] != expected_split or root["fold"] != expected_fold:
            raise ValueError("Frozen split mismatch")
        for candidate in root["candidates"]:
            # The old thirteen-dimensional observations are intentionally discarded.
            # Only pure after-board occupancy determines this candidate's pattern.
            candidate["features"] = pattern_counts(candidate["post_rows"])
    return manifest, dataset, native_definition, counts


def audit(args):
    registration = json.loads(args.coordinator_plan.read_text())
    if (registration["data"]["external_groups"] != list(EXTERNAL_GROUPS) or
            registration["optimization"]["regularization_grid"] != list(REGULARIZERS) or
            registration["optimization"]["steps_per_fit"] != STEPS):
        raise ValueError("Coordinator preregistration differs from this fixed experiment")
    manifest, dataset, native_definition, counts = load_pattern_candidates(args.manifest, args.candidates)
    output = args.manifest.parent
    if (output / "preregistration.json").exists() or (output / "pattern_audit.json").exists():
        raise ValueError("Do not overwrite a frozen pattern audit/plan")
    by_group = Counter(root["opponent_group"] for root in dataset)
    by_round = Counter(root["group"] for root in dataset)
    by_fold = Counter(root["fold"] for root in dataset if root["split"] == "development")
    occurrence = [0] * 144
    candidate_count = 0
    for root in dataset:
        for candidate in root["candidates"]:
            candidate_count += 1
            for i, count in enumerate(candidate["features"]):
                occurrence[i] += count
    report = {
        "fit_performed": False, "ranking_reported": False, "native_counts": counts,
        "retained_roots": len(dataset), "retained_candidates": candidate_count,
        "retained_opponent_groups": dict(by_group), "retained_rounds": dict(by_round),
        "retained_development_folds": dict(by_fold), "pattern_count_occurrences": occurrence,
        "pattern_definition": PATTERN_DEFINITION, "native_feature_definition": native_definition,
        "manifest_sha256": digest(args.manifest), "candidates_sha256": digest(args.candidates),
    }
    write_json(output / "pattern_audit.json", report)
    plan = {
        "schema_version": 1, "status": "prepared_awaiting_coordinator_confirmation_before_fit",
        "scope": "conditional_selected_family", "enabled_in_production": False,
        "pattern_definition": PATTERN_DEFINITION,
        "formula": "fixed_reward + old_terrain + 560 * dot(theta, raw_pattern_counts)",
        "base_policy": "All existing immediate rewards and old terrain are fixed; the thirteen-feature learned correction is absent",
        "score_unit": SCORE_UNIT, "theta_box_bound": BOX_BOUND, "theta_l1_bound": L1_BOUND,
        "table_score_box_bound": 80.0, "table_score_l1_bound": 1120.0,
        "theta_zero_class": 0.0, "normalization": "none", "steps": STEPS,
        "regularization_grid": list(REGULARIZERS),
        "objective": "Pairwise logistic loss plus lambda/2 * dot(theta,theta)",
        "training_mass": "Each retained replay round equal; roots within that round equal; each root's positive-negative pairs equal",
        "negative_policy": "Only production_present alternatives; at most 64 deterministic negatives, stratified by lines/spin/mini with round-robin strata",
        "positive_policy": "Exact native geometry/spin/clear/postboard and production presence required; no distinct negative rejects root",
        "duplicates": "Remove every occurrence of repeated conditional root state globally before fitting",
        "development_fold_assignment": manifest["development_fold_assignment"],
        "pre_native_group_root_counts": manifest["pre_native_group_root_counts"],
        "fold_policy": manifest["fold_policy"], "external_opponent_groups": list(EXTERNAL_GROUPS),
        "model_selection": "Largest OOF opponent-group macro MRR across all three folds; exact ties prefer stronger regularization",
        "metric_aggregation": "Average retained-root rank metrics within each opponent-file group, then average groups equally; per-round metrics also reported",
        "external_policy": "One fit-holdout report after lambda selection and final development fit; not a scientific untouched holdout or win-rate measurement",
        "manifest_sha256": digest(args.manifest), "candidates_sha256": digest(args.candidates),
        "pattern_audit_sha256": digest(output / "pattern_audit.json"),
        "trainer_sha256": digest(Path(__file__)),
        "original_filter_tool_sha256": digest(Path(__file__).with_name("moch_imitation.py")),
        "coordinator_registration": {"path": str(args.coordinator_plan.resolve()),
                                     "sha256": digest(args.coordinator_plan)},
    }
    write_json(output / "preregistration.json", plan)
    print(json.dumps({"fit_performed": False, "counts": counts,
                      "retained_groups": dict(by_group), "folds": dict(by_fold),
                      "plan_sha256": digest(output / "preregistration.json")}, indent=2))


def project(np, unconstrained):
    unconstrained = unconstrained.copy()
    unconstrained[0] = 0.0
    absolute = np.abs(unconstrained)
    clipped = np.minimum(absolute, BOX_BOUND)
    if clipped.sum() > L1_BOUND:
        low, high = 0.0, float(absolute.max())
        for _ in range(48):
            threshold = (low + high) / 2
            mass = np.minimum(BOX_BOUND, np.maximum(0.0, absolute - threshold)).sum()
            if mass > L1_BOUND:
                low = threshold
            else:
                high = threshold
        clipped = np.minimum(BOX_BOUND, np.maximum(0.0, absolute - high))
    return np.sign(unconstrained) * clipped


def fit_coefficients(np, roots, regularization):
    per_round = Counter(root["group"] for root in roots)
    diffs, offsets, masses = [], [], []
    for root in roots:
        positives = [candidate for candidate in root["candidates"] if candidate["positive"]]
        negatives = selected_negatives(root)
        mass = 1.0 / (len(per_round) * per_round[root["group"]] * len(positives) * len(negatives))
        for positive in positives:
            for negative in negatives:
                diffs.append(np.asarray(positive["features"], dtype=float) - negative["features"])
                offsets.append((positive["base_score"] - negative["base_score"]) / SCORE_UNIT)
                masses.append(mass)
    differences, base, weight = np.asarray(diffs), np.asarray(offsets), np.asarray(masses)
    if not np.isclose(weight.sum(), 1.0) or np.any(differences[:, 0]):
        raise ValueError("Training mass or zero-class contract failed")
    theta = np.zeros(144)
    rate = 1.0 / max(1e-9, 0.25 * np.dot(weight, (differences * differences).sum(axis=1)) + regularization)
    for _ in range(STEPS):
        margin = base + differences @ theta
        gradient = differences.T @ (-weight * np.exp(-np.logaddexp(0.0, margin))) + regularization * theta
        theta = project(np, theta - rate * gradient)
    margin = base + differences @ theta
    return {"theta": theta, "objective": float(np.dot(weight, np.logaddexp(0.0, -margin)) + regularization * np.dot(theta, theta) / 2),
            "training_roots": len(roots), "training_rounds": len(per_round), "pairs": len(diffs),
            "regularization": regularization}


def metrics(np, roots, model=None):
    values, corrections = {}, []
    for root in roots:
        candidates = root["candidates"]
        scores = np.asarray([candidate["base_score"] for candidate in candidates])
        if model is not None:
            delta = SCORE_UNIT * (np.asarray([candidate["features"] for candidate in candidates]) @ model["theta"])
            scores += delta
            corrections.extend(np.abs(delta).tolist())
        best = max(float(scores[i]) for i, candidate in enumerate(candidates) if candidate["positive"])
        rank = 1 + int((scores > best + 1e-9).sum())
        values.setdefault(root["opponent_group"], {}).setdefault(root["group"], []).append((rank <= 1, rank <= 5, 1.0 / rank))
    groups = {}
    for group, rounds in values.items():
        per_round = {name: {"roots": len(rs), **{key: sum(row[i] for row in rs) / len(rs)
                                               for i, key in enumerate(("top1", "top5", "mrr"))}}
                     for name, rs in rounds.items()}
        flattened = [value for rs in rounds.values() for value in rs]
        groups[group] = {"rounds": per_round, "root_mean": {
            key: sum(value[i] for value in flattened) / len(flattened)
            for i, key in enumerate(("top1", "top5", "mrr"))}, "round_macro": {
            key: sum(row[key] for row in per_round.values()) / len(per_round)
            for key in ("top1", "top5", "mrr")}}
    macro = {key: sum(group["root_mean"][key] for group in groups.values()) / len(groups) if groups else None
             for key in ("top1", "top5", "mrr")}
    return {"opponent_group_macro": macro, "groups": groups,
            "absolute_correction_p99_score_units": float(np.quantile(corrections, .99)) if corrections else 0.0,
            "absolute_correction_max_score_units": max(corrections, default=0.0)}


def fit(args):
    if args.output.exists():
        raise ValueError("Fit output must be fresh")
    if digest(args.plan) != args.approved_plan_sha256:
        raise ValueError("Explicit plan hash does not match preregistration")
    plan = json.loads(args.plan.read_text())
    checks = {"pattern_definition": PATTERN_DEFINITION, "score_unit": SCORE_UNIT,
              "theta_box_bound": BOX_BOUND, "theta_l1_bound": L1_BOUND,
              "steps": STEPS, "regularization_grid": list(REGULARIZERS),
              "trainer_sha256": digest(Path(__file__)),
              "original_filter_tool_sha256": digest(Path(__file__).with_name("moch_imitation.py")),
              "manifest_sha256": digest(args.manifest), "candidates_sha256": digest(args.candidates)}
    if any(plan[key] != value for key, value in checks.items()):
        raise ValueError("Frozen plan differs from trainer or data")
    if digest(Path(plan["coordinator_registration"]["path"])) != plan["coordinator_registration"]["sha256"]:
        raise ValueError("Coordinator registration changed after data audit")
    manifest, dataset, _, counts = load_pattern_candidates(args.manifest, args.candidates)
    if manifest["development_fold_assignment"] != plan["development_fold_assignment"]:
        raise ValueError("Split differs from frozen plan")
    development = [root for root in dataset if root["split"] == "development"]
    external = [root for root in dataset if root["split"] == "external"]
    if {root["fold"] for root in development} != {0, 1, 2} or {root["opponent_group"] for root in external} != set(EXTERNAL_GROUPS):
        raise ValueError("Missing development fold or external group")
    np = numerical_backend()
    results = []
    for strength in REGULARIZERS:
        folds, group_scores = [], []
        for fold in range(3):
            train = [root for root in development if root["fold"] != fold]
            validation = [root for root in development if root["fold"] == fold]
            model = fit_coefficients(np, train, strength)
            observed = metrics(np, validation, model)
            group_scores.extend(group["root_mean"]["mrr"] for group in observed["groups"].values())
            folds.append({"fold": fold, "validation": observed, "baseline": metrics(np, validation),
                          "objective": model["objective"], "training_rounds": model["training_rounds"]})
        results.append({"regularization": strength, "folds": folds, "selection_mrr": sum(group_scores) / len(group_scores)})
    chosen = max(results, key=lambda result: (result["selection_mrr"], result["regularization"]))
    model = fit_coefficients(np, development, chosen["regularization"])
    theta = model["theta"]
    if theta[0] != 0 or max(abs(theta)) > BOX_BOUND + 1e-12 or sum(abs(theta)) > L1_BOUND + 1e-12:
        raise ValueError("Coefficient constraints failed")
    artifact = {
        "schema_version": 1, "enabled_in_production": False, "scope": "conditional_selected_family",
        "formula": plan["formula"], "pattern_definition": PATTERN_DEFINITION,
        "score_unit": SCORE_UNIT, "theta_box_bound": BOX_BOUND, "theta_l1_bound": L1_BOUND,
        "normalization": "none", "theta": theta.tolist(), "table_scores": (SCORE_UNIT * theta).tolist(),
        "regularization": chosen["regularization"], "steps": STEPS,
        "training_roots": model["training_roots"], "training_rounds": model["training_rounds"],
        "training_pairs": model["pairs"], "preregistration_sha256": digest(args.plan),
        "manifest_sha256": digest(args.manifest), "candidates_sha256": digest(args.candidates),
        "trainer_sha256": digest(Path(__file__)),
        "limits": ["Selected family is given after HOLD", "No pending pressure model",
                   "Official/native and production-coverage filtering", "Correlated replay states",
                   "Fit holdout is not an untouched scientific test", "Ranking is not playing strength"],
    }
    report = {"counts": counts, "cross_validation": results, "chosen_regularization": chosen["regularization"],
              "external_baseline": metrics(np, external), "external_frozen_model": metrics(np, external, model)}
    new_output(args.output)
    write_json(args.output / "model.json", artifact)
    write_json(args.output / "fit_report.json", report)
    print(json.dumps({"output": str(args.output), "regularization": chosen["regularization"],
                      "external_ranking_only": report["external_frozen_model"]["opponent_group_macro"]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prep = commands.add_parser("prepare", help="Write roots and outcome-independent grouped folds")
    prep.add_argument("sources", nargs="+", type=Path)
    prep.add_argument("--output", required=True, type=Path)
    prep.set_defaults(action=prepare)
    for name, action in (("audit", audit), ("fit", fit)):
        command = commands.add_parser(name)
        command.add_argument("--manifest", required=True, type=Path)
        command.add_argument("--candidates", required=True, type=Path)
        if name == "audit":
            command.add_argument("--coordinator-plan", required=True, type=Path)
        if name == "fit":
            command.add_argument("--plan", required=True, type=Path)
            command.add_argument("--approved-plan-sha256", required=True)
            command.add_argument("--output", required=True, type=Path)
        command.set_defaults(action=action)
    args = parser.parse_args()
    args.action(args)


if __name__ == "__main__":
    try:
        main()
    except (KeyError, ValueError, OSError, ImportError) as error:
        print(f"moch_pattern_imitation: {error}", file=sys.stderr)
        sys.exit(2)
