#!/usr/bin/env python3
"""Offline pre-break counterfactuals; conditional legal witnesses, NOT win estimates."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import unicodedata


def fields(line):
    result = {}
    for word in line.split()[1:]:
        key, value = word.split('=', 1)
        if key in result:
            raise ValueError('Duplicate log field: ' + key)
        result[key] = value
    return result


def extract_all(text):
    """Validate and join every root/placement in a complete source game."""
    records, pending, final = [], None, None
    for line_no, line in enumerate(text.splitlines(), 1):
        if line.startswith('TETR_BOT_DIAGNOSTIC side=first '):
            if pending is not None:
                raise ValueError('Unmatched diagnostic')
            root = fields(line)
            if root.get('trace_version') != '1':
                raise ValueError('Missing complete root trace')
            pending = dict(line=line_no, raw=line, root=root)
        elif line.startswith('TETR_TEST_PLACEMENT side=first '):
            event = fields(line)
            if pending is None:
                raise ValueError('Missing root for placement')
            root = pending['root']
            if int(root['pieces']) != len(records) or int(event['total_pieces']) != len(records) + 1:
                raise ValueError('Truncated or out-of-order placements')
            if int(root['root_b2b']) != int(event['b2b_before']):
                raise ValueError('Root/placement B2B mismatch')
            if 'IJLOSTZ'[int(root['selected'])] != unicodedata.normalize('NFKC', event['piece']):
                raise ValueError('Root/placement piece mismatch')
            pending['event'] = event
            records.append(pending)
            pending = None
        elif line.startswith('TETR_TEST_RESULT side=first '):
            final = fields(line)
        elif line.startswith('TETR_LOCAL_VERSUS_RESULT '):
            final = dict(pieces=fields(line)['first_pieces'])
    if pending is not None or final is None or int(final['pieces']) != len(records):
        raise ValueError('Incomplete source game')
    return records


def extract(text, lookback=0):
    """Select by observed breaks; feed only each root and its observed first move."""
    records = extract_all(text)
    selected = {}
    for i, record in enumerate(records):
        e = record['event']
        if int(e['b2b_before']) <= 0 or int(e['b2b_after']) != 0:
            continue
        if int(e['lines']) <= 0 or int(e['spin']) or int(e['lines']) >= 4:
            raise ValueError('Not an ordinary B2B-breaking clear')
        for at in range(max(0, i - lookback), i + 1):
            if int(records[at]['root']['root_b2b']) <= 0:
                continue
            selected.setdefault(at, dict(records[at], breaks=[]))['breaks'].append(i)
    return [selected[i] for i in sorted(selected)]


def classify(row):
    live = row['live_first']['best']
    witness = row['preserve']['two_spins']
    prefix = row['preserve']['prefix']
    for result in (row['live_first'], row['free'], row['preserve']):
        for name in ('best', 'two_spins', 'prefix'):
            state = result[name]
            if state and (not state['verified'] or len(state['path']) != state['depth']):
                raise ValueError('Unverified / incomplete witness')
    if witness and (witness['broken'] or witness['spins'] < 2 or witness['depth'] != row['horizon']):
        raise ValueError('Invalid continuous-spin witness')
    comparable = live is not None and witness is not None and live['depth'] == witness['depth']
    higher = comparable and witness['value'] > live['value'] + 1e-6
    # Descriptive finite-horizon dominance. Not a proof against future attacks.
    no_worse = comparable and all(witness[k] <= live[k] + 1e-6
        for k in ('roof', 'peak_roof', 'holes', 'incoming', 'risk')) and all(
            witness[k] >= live[k] for k in ('attack', 'b2b'))
    return dict(full_witness=bool(witness), prefix_only=bool(prefix and not witness),
                comparable=bool(comparable), higher_current_score=bool(higher),
                higher_score_no_worse_measured_state=bool(higher and no_worse),
                witness_geometry_covered=bool(witness and all(s['production_geometry'] for s in witness['path'])),
                witness_exact_variants_covered=bool(witness and all(s['production_spin_variant'] for s in witness['path'])),
                value_delta=(witness['value'] - live['value']) if comparable else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', type=Path)
    parser.add_argument('--binary', type=Path, default=Path('build/bot_chatgpt_spin_probe'))
    parser.add_argument('--depth', type=int, default=5)
    parser.add_argument('--width', type=int, default=64, help='Per spin-count bucket (0, 1, >=2), per depth')
    parser.add_argument('--lookback', type=int, default=0, help='Also inspect up to N pieces before breaking move')
    parser.add_argument('--max-roots', type=int, default=100000)
    parser.add_argument('--at-piece', type=int, action='append', default=[],
                        help='Diagnostic follow-up: select pre-decision placed-piece count; repeatable')
    parser.add_argument('--repeat-check', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.lookback < 0 or args.max_roots < 1 or not 1 <= args.depth <= 7 or not 1 <= args.width <= 4096:
        parser.error('Invalid limits')
    args.output.mkdir(parents=True, exist_ok=False)
    samples, feeds = [], []
    for source in args.inputs:
        roots = extract(source.read_text(encoding='utf-8'), args.lookback)
        for record in roots:
            if args.at_piece and int(record['root']['pieces']) not in args.at_piece:
                continue
            if len(samples) >= args.max_roots:
                break
            event = record['event']
            extra = ' '.join(f'audit_{key}={event[value]}' for key, value in
                [('spin', 'spin'), ('mini', 'mini'), ('lines', 'lines'), ('attack', 'attack'), ('b2b', 'b2b_after')])
            feeds.append(record['raw'] + ' ' + extra)
            samples.append(dict(source=str(source), **record))
    if not samples:
        raise ValueError('No active-chain pre-break roots')
    root_file = args.output / 'roots.log'
    root_file.write_text('\n'.join(feeds) + '\n', encoding='utf-8')
    sources = [args.binary, Path(__file__), *Path('src/bot/chatgpt').rglob('*.h'),
               Path('src/bot/chatgpt/analysis/spin_probe.cpp'), Path('src/bot/bot_zzztoj.cpp'),
               Path('src/bot/native_host.h'), Path('src/rules/attack.cpp'), Path('src/rules/chain.cpp'),
               Path('src/rules/spin.cpp'), Path('zzztoj_standalone/upstream/src/tetris_core.h'),
               Path('zzztoj_standalone/upstream/src/ai_zzz.cpp'),
               Path('zzztoj_standalone/src/rule_tetr_srs_plus.cpp'), *args.inputs, root_file]
    report = dict(note='Conditional on visible root and no new opponent attacks. Unknown garbage-rise branches are censored, not extrapolated. Full graph offline beam; equal horizon, NOT same algorithm/time as production. Absence is not impossibility. No new versus wins measured.',
        parameters={k: str(v) if isinstance(v, Path) else [str(x) for x in v] if isinstance(v, list) else v
                    for k, v in vars(args).items()},
        source_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    (args.output / 'metadata.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    command = [str(args.binary.resolve()), str(root_file), str(args.depth), str(args.width)]
    raw_file = args.output / 'paths.jsonl'
    with raw_file.open('x', encoding='utf-8') as out, (args.output / 'stderr.log').open('x') as err:
        subprocess.run(command, stdout=out, stderr=err, check=True)
    if args.repeat_check:
        with (args.output / 'repeat.jsonl').open('x', encoding='utf-8') as out:
            subprocess.run(command, stdout=out, check=True)
        if raw_file.read_bytes() != (args.output / 'repeat.jsonl').read_bytes():
            raise RuntimeError('Nondeterministic fixed-work counterfactual search')
    rows = [json.loads(line) for line in raw_file.read_text().splitlines()]
    if len(rows) != len(samples):
        raise ValueError('Missing root results')
    totals = Counter()
    report['roots'] = []
    for sample, row in zip(samples, rows):
        if row['pieces'] != int(sample['root']['pieces']) or not row['live_first']['live_matched']:
            raise ValueError('Mismatched result / observed move')
        classification = classify(row)
        totals.update({k: int(v) for k, v in classification.items() if isinstance(v, bool)})
        report['roots'].append(dict(sample=sample, result=row, classification=classification))
    report['summary'] = dict(roots=len(rows), **totals)
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report['summary']), flush=True)


if __name__ == '__main__':
    main()
