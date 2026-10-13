#!/usr/bin/env python3
"""Summarize primary outcomes and secondary tails from benchmark JSONL.

Reporting only: no policy imports this module. Bins are explicit descriptive
conventions, not inferred test categories, a composite score, or an acceptance
criterion. Keep the full histograms alongside the bands.
"""
import argparse
import collections
import json
import pathlib
import statistics


BANDS = {
    "spikes": (4, 12),
    "b2b_releases_including_final": (3, 10),
    "combo_lengths": (2, 6),
    "combo_attacks": (4, 12),
}


def describe_histogram(histogram, small_max, large_min):
    if small_max >= large_min:
        raise ValueError("overlapping bands")
    counts = collections.Counter()
    for key, count in histogram.items():
        value = int(key)
        if value < 0 or not isinstance(count, int) or count < 0:
            raise ValueError("histograms require nonnegative integer values/counts")
        counts[value] += count
    small = sum(n for value, n in counts.items() if value <= small_max)
    large = sum(n for value, n in counts.items() if value >= large_min)
    total = sum(counts.values())
    middle = total - small - large
    return dict(small_max=small_max, large_min=large_min, events=total,
                small=small, middle=middle, large=large,
                fractions={key: n / total if total else 0
                           for key, n in (("small", small), ("middle", middle), ("large", large))},
                histogram={str(k): counts[k] for k in sorted(counts)})


def summarize(records):
    grouped = collections.defaultdict(list)
    for record in records:
        grouped[(record["case"], record["side"], record["bot"], record["width"])].append(record)
    result = []
    for (case, side, bot, width), group in sorted(grouped.items()):
        pieces = sum(r["pieces"] for r in group)
        attack = sum(r["attack"] for r in group)
        garbage = sum(r["garbage_lines"] for r in group)
        distributions = {}
        for name, (small, large) in BANDS.items():
            counts = collections.Counter()
            for record in group:
                counts.update({str(k): v for k, v in record[name].items()})
            distributions[name] = describe_histogram(counts, small, large)
        result.append(dict(case=case, side=side, bot=bot, width=width,
            seeds=[r["seed"] for r in group], games=len(group),
            primary=dict(outcomes=dict(collections.Counter(r["outcome"] for r in group)),
                         mean_pieces=pieces / len(group),
                         median_pieces=statistics.median(r["pieces"] for r in group),
                         total_pieces=pieces, garbage_lines=garbage,
                         attack_per_piece=attack / pieces if pieces else 0,
                         pieces_per_garbage_line=pieces / garbage if garbage else None),
            secondary=distributions))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=pathlib.Path, help="Benchmark output directories")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    report = dict(priority="primary completion/wins/efficiency first; secondary bands never override them",
                  bands=BANDS, runs={})
    for directory in args.inputs:
        source = directory / "results.jsonl"
        records = [json.loads(line) for line in source.read_text(encoding="utf-8").splitlines()]
        report["runs"][str(directory)] = summarize(records)
    # New output only, just like the benchmark runner.
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write("\n")


if __name__ == "__main__":
    main()
