#!/usr/bin/env python3
"""Independently compare compiled runtime terrain values with frozen observations.

Expected features come from the already-frozen native candidate export, not from
the new runtime header. Expected corrections use model.json's original normalized
formula, not the runtime header's coefficients. The subprocess receives boards
only. No move enumeration, search, fitting, or production changes occur here.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--candidates", required=True, type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    model = json.loads(args.model.read_text())
    if sha256(args.candidates) != model["candidates_sha256"]:
        raise ValueError("Candidate file does not match frozen model provenance")
    names = model["feature_names"]
    if len(names) != 13 or len(set(names)) != 13:
        raise ValueError("Expected thirteen unique frozen features")
    if any(len(model[k]) != len(names) for k in ("mean", "std", "theta")):
        raise ValueError("Truncated frozen normalization or weights")
    if any(not math.isfinite(v) for k in ("mean", "std", "theta") for v in model[k]):
        raise ValueError("Nonfinite model parameter")
    if any(v <= 0 for v in model["std"]):
        raise ValueError("Nonpositive standard deviation")
    if args.output.exists():
        raise ValueError("Use a fresh output directory")
    args.output.mkdir(parents=True)
    runtime_metadata = json.loads(subprocess.check_output([str(args.binary.resolve()), "--metadata"], text=True))
    expected_metadata = {key: model[key] for key in ("score_unit", "mean", "std", "theta")}
    expected_metadata["model_sha256"] = sha256(args.model)
    expected_metadata["selector_weights"] = model["feature_definition"]["selector_weights"]
    if runtime_metadata != expected_metadata:
        write_json(args.output / "metadata_mismatch.json", {"expected": expected_metadata, "runtime": runtime_metadata})
        raise ValueError("Static runtime parameters differ from the frozen model")
    write_json(args.output / "runtime_metadata.json", runtime_metadata)

    def expected_value(features):
        return model["score_unit"] * sum(
            weight * ((value - mean) / std)
            for value, weight, mean, std in zip(features, model["theta"], model["mean"], model["std"]))

    count = 0
    root_count = 0
    feature_ranges = [[math.inf, -math.inf] for _ in names]
    input_path = args.output / "boards.txt"
    expected_path = args.output / "expected.jsonl"
    with args.candidates.open() as source, input_path.open("w") as boards, expected_path.open("w") as expected:
        for line in source:
            record = json.loads(line)
            root_count += 1
            if record["feature_definition"] != model["feature_definition"]:
                raise ValueError("Candidate feature definition differs from the frozen model")
            # Include candidates from rejected roots and outside the production
            # enumerator as well; every exported after-board is a geometry case.
            for candidate_index, candidate in enumerate(record["candidates"]):
                rows = candidate["post_rows"]
                if len(rows) != 40 or any(type(v) is not int or not 0 <= v <= 1023 for v in rows):
                    raise ValueError("Invalid post-clear board")
                features = [candidate["features"][name] for name in names]
                if any(type(v) is not int for v in features):
                    raise ValueError("Terrain observations must have integer features")
                for bounds, value in zip(feature_ranges, features):
                    bounds[0], bounds[1] = min(bounds[0], value), max(bounds[1], value)
                boards.write(str(count) + " " + " ".join(map(str, rows)) + "\n")
                expected.write(json.dumps({"id": count, "sample_id": record["sample_id"],
                                           "candidate_index": candidate_index, "features": features,
                                           "correction": expected_value(features)}) + "\n")
                count += 1
        # This independently specified geometry boundary need not occur in the
        # recordings. It verifies the nonzero normalization intercept on empty.
        boards.write(str(count) + " " + " ".join(["0"] * 40) + "\n")
        expected.write(json.dumps({"id": count, "sample_id": None, "candidate_index": None,
                                   "case": "empty_board", "features": [0] * len(names),
                                   "correction": expected_value([0] * len(names))}) + "\n")

    actual_path = args.output / "runtime.txt"
    with input_path.open() as boards, actual_path.open("w") as actual:
        subprocess.run([str(args.binary.resolve())], stdin=boards, stdout=actual, check=True)

    maximum_absolute_error = 0.0
    maximum_relative_error = 0.0
    maximum_optimized_vs_complete_error = 0.0
    mirror_different_count = 0
    maximum_mirror_difference = 0.0
    largest_mirror_case = None
    largest_case = None
    mismatches = []
    compared = 0
    with expected_path.open() as expected, actual_path.open() as actual:
        for line in expected:
            reference = json.loads(line)
            runtime_line = actual.readline()
            if not runtime_line:
                raise ValueError("Runtime output ended before all boards were checked")
            columns = runtime_line.split()
            if len(columns) != len(names) + 4 or int(columns[0]) != reference["id"]:
                raise ValueError("Runtime output ID or feature count differs")
            correction = float(columns[1])
            complete_correction = float(columns[2])
            observed_features = [float(v) for v in columns[3:3 + len(names)]]
            mirror_correction = float(columns[-1])
            if any(not math.isfinite(v) for v in [correction, complete_correction, mirror_correction, *observed_features]):
                raise ValueError("Nonfinite runtime output")
            wrong_features = [name for name, a, b in zip(names, observed_features, reference["features"]) if a != b]
            error = max(abs(correction - reference["correction"]), abs(complete_correction - reference["correction"]))
            maximum_optimized_vs_complete_error = max(maximum_optimized_vs_complete_error, abs(correction - complete_correction))
            mirror_difference = abs(mirror_correction - correction)
            if mirror_difference > 1e-9:
                mirror_different_count += 1
            if mirror_difference > maximum_mirror_difference:
                maximum_mirror_difference = mirror_difference
                largest_mirror_case = {"id": reference["id"], "sample_id": reference["sample_id"],
                                       "candidate_index": reference["candidate_index"], "original": correction,
                                       "mirror": mirror_correction}
            relative = error / max(1.0, abs(reference["correction"]))
            if error > maximum_absolute_error:
                maximum_absolute_error = error
                largest_case = {"id": reference["id"], "sample_id": reference["sample_id"],
                                "candidate_index": reference["candidate_index"],
                                "expected": reference["correction"], "runtime": correction}
            maximum_relative_error = max(maximum_relative_error, relative)
            if wrong_features or error > 1e-9 + 1e-12 * abs(reference["correction"]):
                if len(mismatches) < 20:
                    mismatches.append({"id": reference["id"], "sample_id": reference["sample_id"],
                                       "candidate_index": reference["candidate_index"],
                                       "wrong_features": wrong_features,
                                       "expected": reference["correction"], "runtime": correction})
            compared += 1
        if actual.read().strip():
            raise ValueError("Runtime produced additional unaccounted output")
    report = {
        "passed": not mismatches, "source_roots": root_count, "recorded_candidates": count,
        "supplemental_empty_boards": 1, "compared_boards": compared,
        "feature_names": names, "frozen_candidate_feature_ranges": dict(zip(names, feature_ranges)),
        "feature_comparison": "Exact integer equality, including all zero-coefficient features",
        "correction_comparison": "Original model.json normalized formula versus runtime header from board only",
        "static_parameter_comparison": "All mean/std/theta values, score unit, model SHA and selector weights equal",
        "absolute_tolerance": 1e-9, "relative_tolerance": 1e-12,
        "maximum_absolute_error": maximum_absolute_error, "maximum_relative_error": maximum_relative_error,
        "maximum_optimized_vs_complete_error": maximum_optimized_vs_complete_error,
        "mirror_observation_only": {"different_boards": mirror_different_count,
                                    "maximum_difference": maximum_mirror_difference,
                                    "largest_case": largest_mirror_case,
                                    "note": "Frozen primary-well ties can select different feature vectors; symmetry is not asserted"},
        "largest_error_case": largest_case, "first_mismatches": mismatches,
        "model_sha256": sha256(args.model), "candidates_sha256": sha256(args.candidates),
        "runtime_binary_sha256": sha256(args.binary), "verifier_python_sha256": sha256(Path(__file__)),
    }
    write_json(args.output / "report.json", report)
    print(json.dumps({key: report[key] for key in (
        "passed", "recorded_candidates", "compared_boards", "maximum_absolute_error", "first_mismatches")}, indent=2))
    if mismatches:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
