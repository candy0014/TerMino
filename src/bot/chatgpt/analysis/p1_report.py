#!/usr/bin/env python3
"""Bounded P1 diagnosis: 30 retrospective roots, not new match outcomes."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys

from spin_report import extract_all


def is_break(record):
    event = record['event']
    return int(event['b2b_before']) > 0 and int(event['b2b_after']) == 0


def select(inputs):
    if len(inputs) != 3:
        raise ValueError('P1 is bounded to three complete development matches')
    samples = []
    for source, text in inputs:
        records = extract_all(text)
        used = set()
        def take(group, candidates, preferred=()):
            available = [i for i in candidates if i not in used]
            chosen = [i for i in preferred if i in available][:2]
            # Spread the remaining selection across the game, without search-result filtering.
            rest = [i for i in available if i not in chosen]
            if len(chosen) < 2 and rest:
                chosen.append(rest[len(rest) // 3])
            rest = [i for i in rest if i not in chosen]
            if len(chosen) < 2 and rest:
                chosen.append(rest[2 * len(rest) // 3])
            if len(chosen) != 2:
                raise ValueError(f'Insufficient {group} roots in {source}')
            for i in chosen:
                used.add(i)
                samples.append(dict(source=str(source), group=group, **records[i]))
        take('near_death', range(max(0, len(records) - 6), len(records)),
             [len(records) - 1, len(records) - 3])
        take('high_pressure', [i for i, r in enumerate(records)
             if int(r['root']['ready']) >= 4 or int(r['root']['incoming']) >= 8])
        pre_break = [i for i, r in enumerate(records) if int(r['root']['root_b2b']) > 0 and
                     any(is_break(s) for s in records[i:i + 3])]
        # Two already-published diagnostic fixtures; no rule in the playing BOT.
        preferred = [131] if '2026100402' in str(source) else [127] if '2026100403' in str(source) else []
        take('pre_break', pre_break, preferred)
        take('successful_spin', [i for i, r in enumerate(records) if int(r['event']['spin']) and
             int(r['event']['lines']) > 0 and int(r['root']['root_b2b']) > 0])
        low = [i for i, r in enumerate(records) if int(r['root']['incoming']) == 0 and
               int(r['root']['roof']) <= 8 and int(r['event']['lines']) == 0]
        empty_hold = [i for i in low if int(records[i]['root']['root_hold']) == -1]
        take('low_pressure_setup', low, empty_hold[-1:])
    return samples


def classify(row):
    low, high = row['traced']
    plain_low, plain_high = row['plain']
    same_depth = low['depth'] == high['depth']
    better = same_depth and high['value'] > low['value'] + 1e-7
    changed = high['move'] != low['move'] or high['hold'] != low['hold']
    delayed = [step for step in high['path'] if step['created_pass'] > low['passes']]
    boundary = next((step for step in high['path'] if step['expanded_pass'] > low['passes']), None)
    plain_better = plain_low['depth'] == plain_high['depth'] and plain_high['value'] > plain_low['value'] + 1e-7
    return dict(missing_root_geometry=row['missing_root_geometry'] > 0,
        comparable_depth=same_depth, higher_budget_better_score=better,
        higher_budget_changes_first=changed,
        search_allocation_witness=better and changed and bool(delayed),
        plain_higher_budget_better_score=plain_better,
        plain_higher_budget_changes_first=plain_low['move'] != plain_high['move'] or plain_low['hold'] != plain_high['hold'],
        instrumentation_changes_low_budget=low['move'] != plain_low['move'] or low['hold'] != plain_low['hold'],
        projected_garbage_rise=any(s['risen'] > 0 for s in high['path']),
        projected_hidden_active_tail=any(s['hidden_active'] for s in high['path']),
        known_next_mismatch=any(s['next_mismatch'] for s in high['path']),
        delayed_prefix_depth=boundary['depth'] if boundary else None,
        delayed_prefix_sibling_rank=boundary['sibling_rank'] if boundary else None,
        value_delta=high['value'] - low['value'] if same_depth else None)


def without_time(value):
    if isinstance(value, dict):
        return {k: without_time(v) for k, v in value.items() if k != 'ms'}
    if isinstance(value, list):
        return [without_time(v) for v in value]
    return value


def validate_fixed(rows):
    for row in rows:
        for traced, plain in zip(row['traced'], row['plain']):
            for key in ('move', 'hold', 'depth', 'attack', 'b2b', 'risen', 'dead'):
                if traced[key] != plain[key]:
                    raise ValueError('Instrumentation changes fixed-pass result: ' + key)
            if abs(traced['value'] - plain['value']) > 1e-8:
                raise ValueError('Instrumentation changes fixed-pass score')


def audit_live_first(sample, row):
    root, event = sample['root'], sample['event']
    observed = [int(root[k]) for k in ('selected', 'row', 'col', 'rotation')]
    observed += [int(event[k]) for k in ('spin', 'mini')]
    result = row['traced'][0]
    matched = result['move'] == observed
    mismatch = None
    if matched:
        step = result['path'][0]
        mismatch = [step[k] for k in ('lines', 'attack', 'b2b')] != [
            int(event[k]) for k in ('lines', 'attack', 'b2b_after')]
    return dict(live_first_matches=matched, live_first_rule_mismatch=mismatch)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs=3, type=Path)
    parser.add_argument('--binary', type=Path, default=Path('build/bot_chatgpt_native_probe'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.binary = args.binary.resolve(strict=True)
    samples = select([(p, p.read_text(encoding='utf-8')) for p in args.inputs])
    args.output.mkdir(parents=True, exist_ok=False)
    roots = args.output / 'roots.log'
    roots.write_text('\n'.join(s['raw'] for s in samples) + '\n', encoding='utf-8')
    (args.output / 'samples.json').write_text(json.dumps(samples, ensure_ascii=False, indent=2) + '\n')
    sources = [args.binary, Path(__file__), Path(__file__).with_name('spin_report.py'),
               Path(__file__).with_name('native_probe.cpp'), *Path('src/bot/chatgpt').rglob('*.h'),
               Path('src/bot/native_host.h'), Path('zzztoj_standalone/upstream/src/tetris_core.h'),
               Path('zzztoj_standalone/upstream/src/ai_zzz.cpp'), Path('zzztoj_standalone/src/rule_tetr_srs_plus.cpp'),
               Path('src/rules/attack.cpp'), Path('src/rules/chain.cpp'), Path('src/rules/spin.cpp'),
               *args.inputs, roots]
    report = dict(note='Cold native search. Tracing changes time cost; fixed-pass controls verify decision equivalence. '
                  '100/400ms traced checkpoints share one growing tree; plain timed controls start fresh. '
                  'Only first moves are host-reachability certified. Future risen garbage and preview-boundary '
                  'paths retain production approximations. Better predicted score is NOT a match win.',
                  command=sys.argv, platform=platform.platform(),
                  source_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    (args.output / 'metadata.json').write_text(json.dumps(report, indent=2) + '\n')
    runs = {}
    for name, kind, low, high in [('timed', 'timed', 100, 400), ('timed_repeat', 'timed', 100, 400),
                                  ('fixed', 'passes', 40, 150), ('fixed_repeat', 'passes', 40, 150)]:
        command = [str(args.binary.resolve()), str(roots), kind, str(low), str(high)]
        with (args.output / f'{name}.jsonl').open('x') as out, (args.output / f'{name}.stderr').open('x') as err:
            subprocess.run(command, stdout=out, stderr=err, check=True)
        rows = [json.loads(line) for line in (args.output / f'{name}.jsonl').read_text().splitlines()]
        if len(rows) != 30 or any(r['pieces'] != int(s['root']['pieces']) for r, s in zip(rows, samples)):
            raise ValueError('Incomplete or mismatched probe results')
        if kind == 'passes':
            validate_fixed(rows)
        totals = Counter()
        for sample, row in zip(samples, rows):
            row['live_audit'] = audit_live_first(sample, row)
            totals.update({k: int(v) for k, v in (classify(row) | row['live_audit']).items() if isinstance(v, bool)})
        runs[name] = dict(summary=dict(roots=len(rows), **totals), rows=rows)
        print(name, json.dumps(runs[name]['summary']), flush=True)
    if without_time(runs['fixed']['rows']) != without_time(runs['fixed_repeat']['rows']):
        raise ValueError('Fixed-pass rerun is not deterministic')
    report['runs'] = runs
    report['samples'] = samples
    report['repeat_search_witnesses'] = [dict(source=s['source'], pieces=int(s['root']['pieces']), group=s['group'])
        for s, a, b in zip(samples, runs['timed']['rows'], runs['timed_repeat']['rows'])
        if classify(a)['search_allocation_witness'] and classify(b)['search_allocation_witness']]
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print('fixed-pass equivalence and repeat check passed; repeated search witnesses:',
          len(report['repeat_search_witnesses']), flush=True)


if __name__ == '__main__':
    main()
