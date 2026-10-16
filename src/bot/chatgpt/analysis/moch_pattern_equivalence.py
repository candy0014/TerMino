#!/usr/bin/env python3
"""Compare native local-pattern code with independent Python board extraction.

No training occurs. Include every candidate board, regardless of teacher label
or production coverage, and independent boundary fixtures. The C++ subprocess
receives occupancy rows only, never expected features or scores.
"""
import argparse
import json
from pathlib import Path
import random
import subprocess

from moch_imitation import digest, write_json
from moch_pattern_imitation import CANONICAL_CODES, CLASS_BY_CODE, pattern_counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidates", required=True, type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("Use a fresh equivalence output directory")
    args.output.mkdir(parents=True)
    encoding = subprocess.check_output([str(args.binary.resolve()), "--encoding"], text=True).splitlines()
    if len(encoding) != 2 or [int(v) for v in encoding[0].split()] != CLASS_BY_CODE or [int(v) for v in encoding[1].split()] != CANONICAL_CODES:
        raise ValueError("Native 256-code mapping / 144-class order differs")
    total = 0
    roots = 0
    synthetic = 0
    weights = [(1 if i % 2 == 0 else -1) * (i + 1) / 7 for i in range(144)]
    feature_ranges = [[1000, -1] for _ in range(144)]
    with (args.output / "boards.txt").open("w") as boards, (args.output / "expected.jsonl").open("w") as expected:
        def add(rows, source):
            nonlocal total
            features = pattern_counts(rows)
            if features[0] or sum(features) > 50:
                raise ValueError("Python zero-class or window limit violated")
            mirrored = [sum(((row >> x) & 1) << (9 - x) for x in range(10)) for row in rows]
            if pattern_counts(mirrored) != features:
                raise ValueError("Python mirror invariance failed")
            for bounds, value in zip(feature_ranges, features):
                bounds[0], bounds[1] = min(bounds[0], value), max(bounds[1], value)
            boards.write(str(total) + " " + " ".join(map(str, rows)) + "\n")
            expected.write(json.dumps({"id": total, "features": features,
                                       "value": sum(count * weight for count, weight in zip(features, weights)),
                                       "source": source}) + "\n")
            total += 1
        with args.candidates.open() as source:
            for line in source:
                record = json.loads(line)
                roots += 1
                for i, candidate in enumerate(record["candidates"]):
                    add(candidate["post_rows"], {"sample_id": record["sample_id"], "candidate": i})
        candidate_count = total
        fixtures = [[0] * 40, [1023] * 40, [1023] + [0] * 39,
                    [0] * 39 + [1022], [1] * 40, [512] * 40,
                    [1 << (y % 10) for y in range(40)],
                    [341 if y % 2 else 682 for y in range(40)]]
        rng = random.Random(20260926)
        # Geometry-only randomized fixtures deliberately include top-boundary
        # occupancy absent from the low-stack training filter.
        fixtures.extend([[rng.randrange(1024) for _ in range(40)] for _ in range(128)])
        for i, rows in enumerate(fixtures):
            add(rows, {"fixture": i})
            synthetic += 1
    with (args.output / "boards.txt").open() as source, (args.output / "runtime.txt").open("w") as output:
        subprocess.run([str(args.binary.resolve())], stdin=source, stdout=output, check=True)
    maximum_error = 0.0
    checked = 0
    with (args.output / "expected.jsonl").open() as source, (args.output / "runtime.txt").open() as output:
        for line in source:
            expected = json.loads(line)
            values = output.readline().split()
            if len(values) != 146 or int(values[0]) != expected["id"]:
                raise ValueError("Native output count/ID mismatch")
            if [int(v) for v in values[1:145]] != expected["features"]:
                raise ValueError("Native/Python counts mismatch at " + str(expected["source"]))
            error = abs(float(values[-1]) - expected["value"])
            if error > 1e-9:
                raise ValueError("Native/Python table score mismatch")
            maximum_error = max(maximum_error, error)
            checked += 1
        if output.read().strip():
            raise ValueError("Additional native output")
    report = {"passed": True, "source_roots": roots, "candidate_boards": candidate_count,
              "supplementary_geometry_boards": synthetic, "compared_boards": checked,
              "all_256_code_classes_match": True, "all_144_canonical_codes_match": True,
              "all_144_counts_match_exactly": True, "all_mirror_counts_match_exactly": True,
              "synthetic_nonzero_class_zero_weight_ignored": True,
              "maximum_synthetic_table_value_absolute_error": maximum_error,
              "feature_ranges": feature_ranges, "fit_performed": False,
              "candidates_sha256": digest(args.candidates), "checker_binary_sha256": digest(args.binary),
              "python_reference_sha256": digest(Path(__file__).with_name("moch_pattern_imitation.py")),
              "verifier_sha256": digest(Path(__file__)),
              "runtime_header_sha256": digest(Path(__file__).parent.parent / "local_pattern_model.h")}
    write_json(args.output / "report.json", report)
    print(json.dumps({k: report[k] for k in ("passed", "candidate_boards", "compared_boards",
                                            "maximum_synthetic_table_value_absolute_error")}, indent=2))


if __name__ == "__main__":
    main()
