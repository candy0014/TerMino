#!/usr/bin/env python3
"""Bounded offline terrain imitation. Never imported by the playing bot.

prepare: read existing official-engine observations and write conditional roots.
audit: validate a complete native candidate export, without optimization.
fit: explicit, single-threaded bounded linear correction fit; no matches/search.

The demonstrated piece family is given. HOLD policy and hidden previews are not
training inputs. A model artifact is a proposal, not an enabled playing policy.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import sys


ORACLE_SHA = "b310aaf13ac0d7ee73c87c58abcdec87e1267217e182c10f4c9cb40dc5d73c92"
PIECES = "ijlostz"  # Host indices: I J L O S T Z.
BASE_SHAPES = {
    "i": ("....", "iiii", "....", "...."),
    "j": ("j..", "jjj", "..."),
    "l": ("..l", "lll", "..."),
    "o": ("oo", "oo"),
    "s": (".ss", "ss.", "..."),
    "t": (".t.", "ttt", "..."),
    "z": ("zz.", ".zz", "..."),
}
FEATURE_NAMES = (
    "aggregate_height", "maximum_height", "shallow_cavities", "buried_cavities",
    "extra_cover", "roughness", "steepness", "secondary_wells", "excessive_well",
    "ready_well_rows", "primary_well_depth", "occupied_cells", "removable_bridge_cavities",
)
SCORE_UNIT = 560.0
THETA_BOUND = 0.5
THETA_L1 = 1.0


class Rejected(ValueError):
    """A source observation outside the declared training scope."""


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def integer(value, name: str, lo: int, hi: int) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name}: expected integer")
    if not math.isfinite(value) or value != int(value) or not lo <= value <= hi:
        raise ValueError(f"{name}: outside [{lo}, {hi}]")
    return int(value)


def board_rows(snapshot) -> list[str]:
    rows = snapshot["rows"]
    if len(rows) != 40 or any(not isinstance(r, str) or len(r) != 10 for r in rows):
        raise ValueError("Expected 40 rows of width 10")
    if any(c not in ".ijloszt#" for row in rows for c in row):
        raise ValueError("Unknown board cell encoding")
    return rows


def masks(rows: list[str]) -> list[int]:
    return [sum(1 << x for x, c in enumerate(row) if c != ".") for row in reversed(rows)]


def official_cells(piece) -> list[tuple[int, int]]:
    """Four (column, official top-origin row) cells; no native pose assumptions.

    The reviewed official tetromino matrices use dx=dy=1, except O dx=0.
    Lock Y is fractional: the engine's occupied-row convention is ceil(y).
    These are standard tetromino geometry data, not an alternate move engine.
    """
    family = piece["type"]
    if family not in BASE_SHAPES:
        raise ValueError("Unknown piece family")
    x = integer(piece["x"], "lock x", -4, 13)
    rotation = integer(piece["r"], "lock rotation", 0, 3)
    y = piece["y"]
    if not isinstance(y, (int, float)) or not math.isfinite(y):
        raise ValueError("Invalid lock Y")
    shape = BASE_SHAPES[family]
    if family != "o":
        for _ in range(rotation):
            shape = tuple("".join(row) for row in zip(*shape[::-1]))
    dx = 0 if family == "o" else 1
    cells = [(x + cx - dx, math.ceil(y) + cy - 1)
             for cy, row in enumerate(shape) for cx, value in enumerate(row) if value != "."]
    if len(set(cells)) != 4 or any(not 0 <= cx < 10 or not 0 <= cy < 40 for cx, cy in cells):
        raise Rejected("teacher_cells_outside_board")
    return cells


def visible_next(snapshot, preview_count: int) -> list[str]:
    # Legacy `next` contains one extra bag entry in these files. Never expose it.
    result = snapshot["next"][:preview_count + 1]
    if len(result) != preview_count + 1 or any(p not in PIECES for p in result):
        raise ValueError("Truncated/invalid visible preview")
    if "visible_next" in snapshot and snapshot["visible_next"] != result:
        raise ValueError("Explicit and legacy visible queues disagree")
    return result


def prepare_root(round_record, placement, previous, max_roof: int):
    options = round_record["options"]
    if options.get("g", 0) != 0 or options.get("spinbonuses") != "all-mini+":
        raise Rejected("unsupported_gravity_or_spin_rules")
    if options.get("nextcount", 5) != 5 or options.get("allow180") is not True:
        raise Rejected("unsupported_preview_or_rotation_rules")
    if options.get("kickset", "SRS+") != "SRS+":
        raise Rejected("nonstandard_official_kickset")
    if options.get("infinite_hold", False):
        raise Rejected("infinite_hold")
    if previous is None:
        raise Rejected("first_placement_no_predecessor")
    before, after = placement["before"], placement["after"]
    prev_after = previous["after"]
    before_rows, after_rows = board_rows(before), board_rows(after)
    if before_rows != board_rows(prev_after):
        raise Rejected("board_changed_between_locks")
    index = integer(placement["index"], "placement index", 1, 1000000)
    if (before["stats"]["piecesplaced"] != index - 1 or
            after["stats"]["piecesplaced"] != index or previous["index"] != index - 1):
        raise ValueError("Broken placement counter sequence")
    for key in ("piecesplaced", "lines", "btb", "combo"):
        if before["stats"][key] != prev_after["stats"][key]:
            raise Rejected("gameplay_state_changed_between_locks")
    if before["pending"] != 0 or prev_after["pending"] != 0:
        raise Rejected("pending_pressure")
    if before["stats"]["garbage"]["received"] != prev_after["stats"]["garbage"]["received"]:
        raise Rejected("garbage_received_between_locks")
    if placement["delta"]["garbageRowsAdded"] != 0:
        raise Rejected("garbage_rows_added")
    roof = max((40 - i for i, row in enumerate(before_rows) if any(c != "." for c in row)), default=0)
    if roof > max_roof:
        raise Rejected("high_stack_spawn_context")
    preview = visible_next(before, 5)
    prior_preview = visible_next(prev_after, 5)
    if placement["piece"]["type"] != preview[0]:
        raise ValueError("Lock family differs from recorded current piece")
    cells = official_cells(placement["piece"])
    attached = [list(row) for row in before_rows]
    for x, y in cells:
        if attached[y][x] != ".":
            raise Rejected("teacher_overlaps_stack")
        attached[y][x] = placement["piece"]["type"]
    remaining = ["".join(row) for row in attached if "." in row]
    lines = 40 - len(remaining)
    reconstructed = ["." * 10] * lines + remaining
    if lines != placement["delta"]["lines"]:
        raise Rejected("official_pure_clear_line_mismatch")
    if reconstructed != after_rows:
        raise Rejected("official_pure_clear_board_mismatch")
    clears = placement["delta"]["clears"]
    spin = any("spin" in k and v for k, v in clears.items())
    mini = any(k.startswith("mini") and v for k, v in clears.items())
    if spin != placement["spin"]:
        raise ValueError("Recorded spin flag differs from clear statistics")
    garbage_height = max((40 - i for i, row in enumerate(before_rows) if "#" in row), default=0)
    return {
        "piece": PIECES.index(preview[0]), "board": masks(before_rows),
        "cells": sorted((x, 39 - y) for x, y in cells), "after": masks(after_rows),
        "lines": lines, "spin": int(spin), "mini": int(mini), "roof": roof,
        "b2b": before["stats"]["btb"], "combo": before["stats"]["combo"],
        "garbage_height": garbage_height, "previous_cleared": int(previous["delta"]["lines"] > 0),
        "after_b2b": after["stats"]["btb"], "after_combo": after["stats"]["combo"],
        "attack": placement["delta"]["attack"],
        "post_hold_visible_current_and_next": preview,
        "prior_spawn_visible_current_and_next": prior_preview,
        "observed_hold_or_family_change": before["hold"] != prev_after["hold"] or preview[0] != prior_preview[0],
    }


def root_line(sample_id: int, index: int, root: dict) -> str:
    csv = lambda values: ",".join(map(str, values))
    fields = {
        "trace_version": 1, "side": "first", "sample_id": sample_id,
        "pieces": index - 1, "board": csv(root["board"]), "root_next": root["piece"],
        "root_hold": -2, "root_row": -2, "root_col": 4, "root_rotation": 0,
        "root_b2b": root["b2b"], "root_combo": root["combo"],
        "root_garbage_height": root["garbage_height"], "root_cleared": root["previous_cleared"],
        "incoming": 0, "ready": 0, "forecast_step": 1, "root_packets": "-",
        "opener_double": 1, "selected": root["piece"], "row": -2, "col": 4, "rotation": 0,
        "teacher_cells": ",".join(f"{x}:{y}" for x, y in root["cells"]),
        "teacher_after": csv(root["after"]), "teacher_lines": root["lines"],
        "teacher_spin": root["spin"], "teacher_mini": root["mini"],
        "teacher_attack": root["attack"], "teacher_b2b": root["after_b2b"],
        "teacher_combo": root["after_combo"],
    }
    return "TETR_BOT_DIAGNOSTIC " + " ".join(f"{k}={v}" for k, v in fields.items())


def new_output(path: Path):
    if path.exists():
        raise ValueError(f"Output already exists: {path}; use a fresh output path")
    path.mkdir(parents=True)


def write_json(path: Path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def prepare(args):
    rounds, sources = [], []
    for source in sorted(args.sources, key=lambda p: str(p.resolve())):
        data = json.loads(source.read_text())
        if data.get("reviewedSha256") != ORACLE_SHA:
            raise ValueError(f"Unreviewed reference export: {source}")
        sources.append({"path": str(source.resolve()), "sha256": digest(source)})
        for round_record in data["rounds"]:
            if round_record["username"] == args.teacher:
                rounds.append(round_record)
    rounds.sort(key=lambda r: (r["file"], r["round"], r["side"]))
    seen, counts, samples, roots = set(), Counter(), [], []
    for round_record in rounds:
        group = f"{round_record['file']}:{round_record['round']}:{args.teacher}"
        if group in seen:
            raise ValueError("Duplicate replay round: " + group)
        seen.add(group)
        if round_record["file"] == "mochengine2vszzz.ttrm":
            round_number = integer(round_record["round"], "vs zzz round", 1, 9)
            split, fold = "development", (round_number - 1) // 3
        elif round_record["file"] in ("talothebot.ttrm", "5han.ttrm"):
            split, fold = "external", None
        else:
            raise ValueError("No predefined split for " + round_record["file"])
        placements = round_record["placements"]
        for i, placement in enumerate(placements):
            counts["teacher_observations"] += 1
            try:
                root = prepare_root(round_record, placement, placements[i - 1] if i else None, args.max_roof)
            except Rejected as error:
                counts["reject:" + str(error)] += 1
                continue
            sample_id = len(samples)
            index = integer(placement["index"], "placement index", 1, 1000000)
            roots.append(root_line(sample_id, index, root))
            # Outcome-independent identity; duplicates share a group during fit.
            state_key = {k: root[k] for k in ("piece", "board", "b2b", "combo", "garbage_height", "previous_cleared")}
            state_hash = hashlib.sha256(json.dumps(state_key, sort_keys=True).encode()).hexdigest()
            samples.append({"sample_id": sample_id, "group": group, "source": round_record["file"],
                            "round": round_record["round"], "placement": index, "split": split,
                            "fold": fold, "state_sha256": state_hash, "scope": "conditional_selected_family",
                            "root_line_sha256": hashlib.sha256(roots[-1].encode()).hexdigest(),
                            "visible_next_count": 5,
                            "observed_hold_or_family_change": root["observed_hold_or_family_change"],
                            "post_hold_visible_current_and_next": root["post_hold_visible_current_and_next"],
                            "prior_spawn_visible_current_and_next": root["prior_spawn_visible_current_and_next"]})
            counts[f"eligible:{split}:{fold}"] += 1
    if not roots:
        raise ValueError("No eligible roots")
    new_output(args.output)
    (args.output / "roots.log").write_text("\n".join(roots) + "\n")
    write_json(args.output / "manifest.json", {
        "schema_version": 1, "scope": "conditional_selected_family", "teacher": args.teacher,
        "source_exports": sources, "oracle_sha256": ORACLE_SHA,
        "roots_sha256": digest(args.output / "roots.log"), "max_roof": args.max_roof,
        "queue_policy": "current plus five retained for audit only; native input contains selected family only",
        "counts": dict(counts), "samples": samples,
    })
    print(json.dumps({"output": str(args.output), "counts": dict(counts)}, indent=2))


def finite_number(value, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("Nonfinite/non-numeric " + name)
    return float(value)


def load_candidates(manifest_path: Path, candidate_path: Path):
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema_version") != 1 or manifest.get("scope") != "conditional_selected_family":
        raise ValueError("Unsupported manifest")
    roots_path = manifest_path.with_name("roots.log")
    if not roots_path.is_file() or digest(roots_path) != manifest["roots_sha256"]:
        raise ValueError("Manifest/root input hash mismatch")
    samples = {s["sample_id"]: s for s in manifest["samples"]}
    if len(samples) != len(manifest["samples"]):
        raise ValueError("Duplicate manifest IDs")
    observed, dataset, counts = set(), [], Counter()
    feature_definition = None
    for line in candidate_path.read_text().splitlines():
        record = json.loads(line)
        if record.get("schema_version") != 1:
            raise ValueError("Unsupported native candidate schema")
        sample_id = integer(record["sample_id"], "sample_id", 0, len(samples) - 1)
        if sample_id not in samples or sample_id in observed:
            raise ValueError("Unknown or duplicate native sample ID")
        observed.add(sample_id)
        if hashlib.sha256(record["input_root_line"].encode()).hexdigest() != samples[sample_id]["root_line_sha256"]:
            raise ValueError("Native candidate/root-manifest mismatch")
        definition = record["feature_definition"]
        if feature_definition is None:
            feature_definition = definition
        elif definition != feature_definition:
            raise ValueError("Mixed native feature definitions or selector weights")
        if set(definition["feature_names"]) != set(FEATURE_NAMES):
            raise ValueError("Unexpected feature schema")
        if record.get("reject_reason"):
            counts["native_reject:" + record["reject_reason"]] += 1
            continue
        candidates = record["candidates"]
        if not candidates:
            raise ValueError("Accepted root has no candidates")
        kept = []
        for candidate in candidates:
            if set(candidate["features"]) != set(FEATURE_NAMES):
                raise ValueError("Candidate feature keys differ")
            if type(candidate["positive"]) is not bool or type(candidate["production_present"]) is not bool:
                raise ValueError("Candidate label/coverage must be booleans")
            features = [finite_number(candidate["features"][name], name) for name in FEATURE_NAMES]
            fixed = finite_number(candidate["fixed_reward"], "fixed_reward")
            old = finite_number(candidate["old_terrain"], "old_terrain")
            if not candidate["production_present"]:
                counts["candidate_not_in_production"] += 1
                continue
            kept.append({"features": features, "base_score": fixed + old,
                         "positive": candidate["positive"], "lines": candidate["lines"],
                         "spin": candidate["spin"], "mini": candidate["mini"],
                         "post_rows": candidate["post_rows"]})
        if not any(c["positive"] for c in kept):
            counts["native_reject:production_teacher_missing"] += 1
            continue
        if not any(not c["positive"] for c in kept):
            counts["native_reject:no_distinct_production_alternative"] += 1
            continue
        dataset.append({**samples[sample_id], "candidates": kept})
        counts["eligible:" + samples[sample_id]["split"]] += 1
    if observed != set(samples):
        raise ValueError("Incomplete candidate export; not every manifest root is accounted for")
    # Exclude repeated states entirely, rather than leaking identical examples
    # across grouped folds or giving repeated openings disproportionate weight.
    frequencies = Counter(r["state_sha256"] for r in dataset)
    unique = [r for r in dataset if frequencies[r["state_sha256"]] == 1]
    counts["duplicate_state_roots_removed"] = len(dataset) - len(unique)
    counts["unique_development_roots"] = sum(r["split"] == "development" for r in unique)
    counts["unique_external_roots"] = sum(r["split"] == "external" for r in unique)
    return manifest, unique, feature_definition, dict(counts)


def audit(args):
    _, dataset, definition, counts = load_candidates(args.manifest, args.candidates)
    print(json.dumps({"fit_performed": False, "counts": counts, "feature_definition": definition,
                      "groups": dict(Counter(r["group"] for r in dataset))}, indent=2))


def numerical_backend():
    # Import only inside the explicit fit command. Do not reserve BLAS workers
    # during prepare/audit or compete with an unpinned live match.
    for key in ("OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS"):
        os.environ[key] = "1"
    import numpy as np
    return np


def normalization(np, roots):
    # One root contributes equal mass regardless of its candidate count.
    first = np.zeros(len(FEATURE_NAMES))
    second = np.zeros(len(FEATURE_NAMES))
    for root in roots:
        values = np.asarray([c["features"] for c in root["candidates"]], dtype=float)
        first += values.mean(axis=0)
        second += (values * values).mean(axis=0)
    mean = first / len(roots)
    std = np.sqrt(np.maximum(0, second / len(roots) - mean * mean))
    std[std < 1e-9] = 1.0
    return mean, std


def selected_negatives(root, limit=64):
    negatives = [c for c in root["candidates"] if not c["positive"]]
    buckets = {}
    for c in negatives:
        key = (c["lines"], c["spin"], c["mini"])
        buckets.setdefault(key, []).append(c)
    for key, values in buckets.items():
        values.sort(key=lambda c: hashlib.sha256(json.dumps([root["sample_id"], key, c["post_rows"],
                                                            c["features"]], sort_keys=True).encode()).hexdigest())
    selected = []
    while len(selected) < limit and any(buckets.values()):
        for key in sorted(buckets):
            if buckets[key] and len(selected) < limit:
                selected.append(buckets[key].pop())
    return selected


def fit_coefficients(np, roots, regularization: float, steps: int):
    mean, std = normalization(np, roots)
    diffs, offsets, masses = [], [], []
    for root in roots:
        positives = [c for c in root["candidates"] if c["positive"]]
        negatives = selected_negatives(root)
        mass = 1.0 / (len(roots) * len(positives) * len(negatives))
        for positive in positives:
            for negative in negatives:
                diffs.append((np.asarray(positive["features"]) - negative["features"]) / std)
                offsets.append((positive["base_score"] - negative["base_score"]) / SCORE_UNIT)
                masses.append(mass)
    differences = np.asarray(diffs)
    base = np.asarray(offsets)
    weight = np.asarray(masses)
    theta = np.zeros(len(FEATURE_NAMES))
    # A conservative global smoothness bound gives a fixed stable step size.
    rate = 1.0 / max(1e-9, 0.25 * np.dot(weight, (differences * differences).sum(axis=1)) + regularization)
    for _ in range(steps):
        margin = base + differences @ theta
        sigmoid_negative = np.exp(-np.logaddexp(0.0, margin))
        gradient = differences.T @ (-weight * sigmoid_negative) + regularization * theta
        unconstrained = theta - rate * gradient
        absolute = np.abs(unconstrained)
        clipped = np.minimum(absolute, THETA_BOUND)
        if clipped.sum() > THETA_L1:
            # Exact box/L1 intersection projection, up to bisection tolerance.
            # Clipping first and projecting only onto L1 is not equivalent.
            low, high = 0.0, float(absolute.max())
            for _ in range(48):
                threshold = (low + high) / 2
                mass = np.minimum(THETA_BOUND, np.maximum(0.0, absolute - threshold)).sum()
                if mass > THETA_L1:
                    low = threshold
                else:
                    high = threshold
            clipped = np.minimum(THETA_BOUND, np.maximum(0.0, absolute - high))
        theta = np.sign(unconstrained) * clipped
    margin = base + differences @ theta
    objective = float(np.dot(weight, np.logaddexp(0.0, -margin)) + regularization * np.dot(theta, theta) / 2)
    return {"mean": mean, "std": std, "theta": theta, "objective": objective,
            "training_roots": len(roots), "pairs": len(diffs), "regularization": regularization}


def metrics(np, roots, model=None):
    groups = {}
    corrections = []
    for root in roots:
        candidates = root["candidates"]
        scores = np.asarray([c["base_score"] for c in candidates])
        if model is not None:
            features = np.asarray([c["features"] for c in candidates])
            correction = SCORE_UNIT * (((features - model["mean"]) / model["std"]) @ model["theta"])
            scores += correction
            corrections.extend(np.abs(correction).tolist())
        positives = [i for i, c in enumerate(candidates) if c["positive"]]
        best = float(max(scores[i] for i in positives))
        # Ties receive the same best achievable rank; duplicates are not losses.
        rank = 1 + int((scores > best + 1e-9).sum())
        groups.setdefault(root["group"], []).append((rank <= 1, rank <= 5, 1.0 / rank))
    per_round = {group: {"roots": len(values), "top1": sum(v[0] for v in values) / len(values),
                         "top5": sum(v[1] for v in values) / len(values),
                         "mrr": sum(v[2] for v in values) / len(values)}
                 for group, values in groups.items()}
    macro = {k: sum(v[k] for v in per_round.values()) / len(per_round) if per_round else None
             for k in ("top1", "top5", "mrr")}
    return {"round_macro": macro, "rounds": per_round,
            "absolute_correction_p99_score_units": float(np.quantile(corrections, 0.99)) if corrections else 0.0,
            "absolute_correction_max_score_units": max(corrections, default=0.0)}


def fit(args):
    if args.output.exists():
        raise ValueError("Use a fresh output directory for each explicit fit")
    manifest, dataset, definition, counts = load_candidates(args.manifest, args.candidates)
    development = [r for r in dataset if r["split"] == "development"]
    external = [r for r in dataset if r["split"] == "external"]
    if {r["fold"] for r in development} != {0, 1, 2} or not external:
        raise ValueError("All three development folds and the external fit holdout are required")
    np = numerical_backend()
    results = []
    # Deliberately fixed grid. No seed, feature, opponent, or search-budget sweep.
    for strength in (0.01, 0.1, 1.0):
        fold_results = []
        for fold in (0, 1, 2):
            train = [r for r in development if r["fold"] != fold]
            validation = [r for r in development if r["fold"] == fold]
            model = fit_coefficients(np, train, strength, args.steps)
            fold_results.append({"fold": fold, "validation": metrics(np, validation, model),
                                 "baseline": metrics(np, validation), "objective": model["objective"]})
        results.append({"regularization": strength, "folds": fold_results,
                        "selection_mrr": sum(f["validation"]["round_macro"]["mrr"] for f in fold_results) / 3})
    # Prefer stronger regularization on exact ties; external roots are not consulted.
    chosen = max(results, key=lambda r: (r["selection_mrr"], r["regularization"]))
    model = fit_coefficients(np, development, chosen["regularization"], args.steps)
    artifact = {
        "schema_version": 1, "enabled_in_production": False, "scope": "conditional_selected_family",
        "formula": "fixed_reward + old_terrain + 560 * dot(theta, (fixed_features - mean) / std)",
        "feature_names": list(FEATURE_NAMES), "feature_definition": definition,
        "selector_policy": "use the exported fixed selector weights; learned coefficients must not reselect wells",
        "score_unit": SCORE_UNIT, "theta_box_bound": THETA_BOUND, "theta_l1_bound": THETA_L1,
        "mean": model["mean"].tolist(), "std": model["std"].tolist(), "theta": model["theta"].tolist(),
        "regularization": chosen["regularization"], "steps": args.steps,
        "training_roots": model["training_roots"], "training_pairs": model["pairs"],
        "manifest_sha256": digest(args.manifest), "candidates_sha256": digest(args.candidates),
        "prepare_roots_sha256": manifest["roots_sha256"],
        "tool_sha256": digest(Path(__file__)),
        "limits": ["No HOLD policy learned", "No pressure/tanking model learned", "Native transfer filtering",
                   "Round-correlated observations", "Inspected fit holdout, not untouched scientific test data",
                   "Imitation ranking is not win rate", "No production integration or match run by this tool"],
    }
    report = {"counts": counts, "cross_validation": results,
              "chosen_regularization": chosen["regularization"],
              "external_baseline": metrics(np, external), "external_frozen_model": metrics(np, external, model)}
    new_output(args.output)
    write_json(args.output / "model.json", artifact)
    write_json(args.output / "fit_report.json", report)
    print(json.dumps({"output": str(args.output), "regularization": chosen["regularization"],
                      "external_ranking_only": report["external_frozen_model"]["round_macro"]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prepare_parser = sub.add_parser("prepare", help="Write conditional roots; no enumeration or fitting")
    prepare_parser.add_argument("sources", nargs="+", type=Path, help="Existing moch_terrain placements JSON")
    prepare_parser.add_argument("--teacher", default="mochbot-dev")
    prepare_parser.add_argument("--max-roof", type=int, choices=range(1, 16), default=15)
    prepare_parser.add_argument("--output", required=True, type=Path)
    prepare_parser.set_defaults(action=prepare)
    for name, action in (("audit", audit), ("fit", fit)):
        command = sub.add_parser(name, help="Validate only" if name == "audit" else "Explicit bounded single-threaded optimization")
        command.add_argument("--manifest", required=True, type=Path)
        command.add_argument("--candidates", required=True, type=Path)
        if name == "fit":
            command.add_argument("--output", required=True, type=Path)
            command.add_argument("--steps", type=int, choices=range(1, 201), default=200)
        command.set_defaults(action=action)
    args = parser.parse_args()
    args.action(args)


if __name__ == "__main__":
    try:
        main()
    except (KeyError, ValueError, OSError, ImportError) as error:
        print(f"moch_imitation: {error}", file=sys.stderr)
        sys.exit(2)
