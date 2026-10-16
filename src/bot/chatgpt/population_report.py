#!/usr/bin/env python3
"""Descriptive population comparison; resample whole FT3 series, not moves.

The two versions use disjoint seed populations. Intervals describe this finite
batch under exchangeable-series assumptions, not proof of universal strength.
FT3 stops on wins, so the pooled game fraction is reported with that caveat.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import random


def wilson(wins, total, z=1.959963984540054):
    if not total:
        return None
    p = wins / total
    d = 1 + z * z / total
    center = (p + z * z / (2 * total)) / d
    half = z * math.sqrt(p * (1 - p) / total + z * z / (4 * total * total)) / d
    return [max(0, center - half), min(1, center + half)]


def fraction(series):
    n = sum(s['wins'] + s['losses'] for s in series)
    return sum(s['wins'] for s in series) / n if n else 0


def bootstrap_difference(baseline, candidate, repetitions=20000):
    if len(baseline) < 2 or len(candidate) < 2:
        return None
    rng = random.Random(25092505)
    values = []
    for _ in range(repetitions):
        b = [rng.choice(baseline) for _ in baseline]
        c = [rng.choice(candidate) for _ in candidate]
        values.append(fraction(c) - fraction(b))
    values.sort()
    # Nearest-rank empirical percentiles, fixed before match observation.
    return [values[math.ceil(repetitions * q) - 1] for q in (0.025, 0.975)]


def solo_summary(rows):
    p = sum(r['pieces'] for r in rows)
    a = sum(r['attack'] for r in rows)
    g = sum(r['garbage_lines'] for r in rows)
    return dict(games=len(rows), outcomes=dict(collections.Counter(r['outcome'] for r in rows)),
                pieces=p, attack=a, garbage_lines=g,
                mean_pieces=p / len(rows), attack_per_piece=a / p,
                pieces_per_garbage_line=p / g if g else None,
                actual_pps=p / sum(r['wall_seconds'] for r in rows),
                attack_plus_final_b2b_per_piece=(a + sum(r['remaining_b2b'] for r in rows)) / p)


def report(manifest):
    series = {'baseline': [], 'candidate': []}
    solo = collections.defaultdict(lambda: collections.defaultdict(list))
    for job in manifest['jobs']:
        directory = Path(job['output'])
        records = [json.loads(s) for s in (directory / 'results.jsonl').read_text().splitlines()]
        first = [r for r in records if r['side'] == 'first']
        if not first or any(r['case'] != job['case'] for r in records):
            raise ValueError('Missing/wrong case records')
        metadata = json.loads((directory / 'metadata.json').read_text())
        if metadata['binary_sha256'] != job['binary_sha256']:
            raise ValueError('Wrong binary')
        if job['suite'] == 'versus':
            score = json.loads((directory / (job['case'] + '_score.json')).read_text())
            w, l = score['first'], score['second']
            if max(w, l) != 3:
                raise ValueError('Incomplete FT3 series')
            if w != sum(r['outcome'] == 'won' for r in first):
                raise ValueError('Score/records mismatch')
            if l != sum(r['outcome'] == 'won' and r['side'] == 'second' for r in records):
                raise ValueError('Opponent score/records mismatch')
            series[job['variant']].append(dict(group=job['group'], seed_start=job['seed'],
                wins=w, losses=l, draws=len(first)-w-l, series_won=w == 3,
                summary=solo_summary(first), output=job['output']))
        else:
            if len(first) != job['repetitions']:
                raise ValueError('Incomplete solo case')
            solo[job['case']][job['variant']].extend(first)
    summaries = {}
    for v, rows in series.items():
        wins = sum(s['wins'] for s in rows); losses = sum(s['losses'] for s in rows)
        series_wins = sum(s['series_won'] for s in rows)
        summaries[v] = dict(series=len(rows), wins=wins, losses=losses,
            draws=sum(s['draws'] for s in rows), game_win_fraction=fraction(rows),
            all_game_win_fraction=wins / sum(s['wins'] + s['losses'] + s['draws'] for s in rows),
            series_wins=series_wins, series_win_fraction=series_wins / len(rows),
            series_win_wilson_95=wilson(series_wins, len(rows)))
    interval = bootstrap_difference(series['baseline'], series['candidate'])
    difference = fraction(series['candidate']) - fraction(series['baseline'])
    signal = (difference >= 0.05 and interval is not None and interval[0] > 0 and
              summaries['candidate']['series_win_fraction'] >= summaries['baseline']['series_win_fraction'])
    return dict(note=__doc__, protocol='FT3; bot_chatgpt:botris quotas1:1.3; disjoint seeds; no paired-group veto',
        game_fraction_definition='game_win_fraction = wins/(wins+losses), excluding draws; all_game_win_fraction includes draws in the denominator. Both and the draw count are reported; FT3 stopping is retained.',
        series=series, versus=summaries, game_win_fraction_difference=difference,
        series_cluster_bootstrap_difference_95=interval, bootstrap_repetitions=20000,
        strength_evidence_gate=signal, default_promotion=False,
        interpretation='Strength signal requires independent confirmation/P4; crossing zero is inconclusive, not equivalence. Secondary comparisons are exploratory and do not override wins.',
        solo={c: {v: solo_summary(rows) for v, rows in versions.items()} for c, versions in solo.items()})


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
