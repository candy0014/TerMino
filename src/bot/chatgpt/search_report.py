#!/usr/bin/env python3
"""Read-only search-work diagnostics, kept separate from primary game scores."""
import argparse
import collections
import hashlib
import json
from pathlib import Path


def summarize(rows):
    if not rows or 'evaluated_depths' not in rows[0]:
        return None  # Historical executable / opponent without these counters.
    depths = [0] * 16
    totals = dict.fromkeys(('search_calls', 'cache_hits', 'visited', 'landings'), 0)
    selected = collections.Counter()
    for row in rows:
        values = [int(x) for x in row['evaluated_depths'].split(',')]
        if len(values) != 16 or min(values) < 0:
            raise ValueError('Invalid depth counters')
        depths = [a + b for a, b in zip(depths, values)]
        counts = {key: int(row[key]) for key in totals}
        if min(counts.values()) < 0 or counts['cache_hits'] > counts['search_calls']:
            raise ValueError('Invalid search counters')
        for key in totals:
            totals[key] += counts[key]
        depth = int(row['selected_depth'])
        if depth < 0:
            raise ValueError('Invalid selected depth')
        selected[depth] += 1
    return dict(decisions=len(rows), totals=totals,
                cache_hit_fraction=totals['cache_hits'] / totals['search_calls'] if totals['search_calls'] else None,
                evaluated_by_depth=depths,
                mean_evaluated_by_depth=[n / len(rows) for n in depths],
                selected_depth_histogram=dict(sorted(selected.items())),
                mean_selected_depth=sum(d * n for d, n in selected.items()) / len(rows))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = dict(note='Depth is zero-based evaluation depth; selected depth counts planned pieces. Work counters are not scores, unique-state counts, or proof of better decisions.', runs={}, source_sha256={})
    def read(path):
        raw = path.read_bytes()
        result['source_sha256'][str(path)] = hashlib.sha256(raw).hexdigest()
        return raw
    read(Path(__file__))
    for directory in args.inputs:
        metadata = json.loads(read(directory / 'metadata.json'))
        records = [json.loads(line) for line in read(directory / 'results.jsonl').splitlines()]
        groups = collections.defaultdict(list)
        for record in records:
            path = directory / f"{record['case']}_seed{record['seed']}.decisions.json"
            decisions = json.loads(read(path)) if path.exists() else {}
            groups[(record['case'], record['side'])].extend(decisions.get(record['side'], []))
        result['runs'][str(directory)] = dict(binary_sha256=metadata['binary_sha256'],
            groups=[dict(case=case, side=side, search=summarize(rows))
                    for (case, side), rows in sorted(groups.items())])
    with args.output.open('x', encoding='utf-8') as output:
        json.dump(result, output, ensure_ascii=False, indent=2)
        output.write('\n')


if __name__ == '__main__':
    main()
