#!/usr/bin/env python3
"""Independent confirmation of a preregistered digging-efficiency signal.

Resamples whole trials independently within each version; never pools the
exploratory trials or treats equal group numbers as paired observations.
Legality/provenance auditing remains a separate required acceptance check.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import random


def ratio(rows):
    garbage = sum(r['garbage_lines'] for r in rows)
    if garbage <= 0:
        raise ValueError('No cleared garbage lines')
    return sum(r['pieces'] for r in rows) / garbage


def bootstrap_difference(baseline, candidate, repetitions=20000):
    if len(baseline) < 2 or len(candidate) < 2:
        raise ValueError('At least two trials per version required')
    if repetitions < 40:
        raise ValueError('Too few bootstrap repetitions')
    rng = random.Random(25092506)
    values = []
    for _ in range(repetitions):
        b = [rng.choice(baseline) for _ in baseline]
        c = [rng.choice(candidate) for _ in candidate]
        values.append(ratio(c) - ratio(b))
    values.sort()
    return [values[math.ceil(repetitions * q) - 1] for q in (0.025, 0.975)]


def summarize(rows):
    pieces = sum(r['pieces'] for r in rows)
    attack = sum(r['attack'] for r in rows)
    return dict(trials=len(rows), completed=sum(r['outcome'] == 'goal' and
        r['garbage_lines'] >= 100 for r in rows),
        outcomes=dict(collections.Counter(r['outcome'] for r in rows)),
        pieces=pieces, garbage_lines=sum(r['garbage_lines'] for r in rows),
        pieces_per_garbage_line=ratio(rows), attack=attack,
        attack_per_piece=attack / pieces,
        actual_pps=pieces / sum(r['wall_seconds'] for r in rows),
        diagnostic_fallbacks=sum(r['diagnostic_fallbacks'] for r in rows))


def metric_gate(baseline, candidate, interval, expected=15):
    return (baseline['trials'] == candidate['trials'] == expected and
        candidate['completed'] == expected and
        candidate['completed'] >= baseline['completed'] and
        candidate['pieces_per_garbage_line'] <= baseline['pieces_per_garbage_line'] * .98 and
        interval[1] < 0 and candidate['diagnostic_fallbacks'] == 0 and
        baseline['diagnostic_fallbacks'] == 0)


def report(manifest):
    expected = manifest['fixed_trials_per_version']
    rows = {'baseline': [], 'candidate': []}
    seeds, outputs = set(), set()
    for job in manifest['jobs']:
        if job['suite'] != 'solo' or job['case'] != 'layer_m1.0' or job['repetitions'] != 1:
            raise ValueError('Wrong confirmation job')
        if job['seed'] in seeds or job['output'] in outputs:
            raise ValueError('Repeated seed or output')
        seeds.add(job['seed']); outputs.add(job['output'])
        directory = Path(job['output'])
        records = [json.loads(s) for s in (directory / 'results.jsonl').read_text().splitlines()]
        if len(records) != 1:
            raise ValueError('Missing/extra trial records')
        row = records[0]
        if (row['side'] != 'first' or row['case'] != job['case'] or row['seed'] != job['seed'] or
            row['parameters']['garbage_goal'] != 100 or row['parameters']['mess'] != 1.0):
            raise ValueError('Wrong trial condition/seed')
        metadata = json.loads((directory / 'metadata.json').read_text())
        if metadata['binary_sha256'] != job['binary_sha256']:
            raise ValueError('Wrong binary')
        rows[job['variant']].append(dict(row, group=job['group'], output=job['output']))
    if any(len(r) != expected for r in rows.values()):
        raise ValueError('Incomplete registered population')
    summaries = {v: summarize(r) for v, r in rows.items()}
    b, c = summaries['baseline'], summaries['candidate']
    interval = bootstrap_difference(rows['baseline'], rows['candidate'])
    return dict(note=__doc__, exploratory_excluded=True, trials=rows, summaries=summaries,
        ratio_difference=c['pieces_per_garbage_line'] - b['pieces_per_garbage_line'],
        relative_reduction=1 - c['pieces_per_garbage_line'] / b['pieces_per_garbage_line'],
        independent_trial_bootstrap_difference_95=interval,
        bootstrap_repetitions=20000, bootstrap_seed=25092506,
        metric_confirmation_gate=metric_gate(b, c, interval, expected),
        legality_audit_required=True, default_promotion=False,
        scope='m1.0 digging only; no inference of versus/other-condition improvement',
        interpretation='Unpassed gate means unconfirmed, not equivalence or proof of no effect.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = report(json.loads(args.manifest.read_text()))
    with args.output.open('x') as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
        f.write('\n')


if __name__ == '__main__':
    main()
