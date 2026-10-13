#!/usr/bin/env python3
"""Cold, fixed-expansion replay of visible roots; not a versus score estimate."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def summarize(rows):
    if len(rows) % 2:
        raise ValueError('Incomplete root pair')
    pairs = list(zip(rows[::2], rows[1::2]))
    changed = []
    for a, b in pairs:
        if (a['line'], a['pieces']) != (b['line'], b['pieces']) or (a['weight'], b['weight']) != (0, 80):
            raise ValueError('Mismatched root comparison')
        if not (a['legal'] and b['legal']):
            raise ValueError('Unreachable replay answer')
        if any(a[k] != b[k] for k in ('selected', 'row', 'col', 'rotation', 'spin', 'mini')):
            changed.append((a, b))
    means = lambda group, index, key: sum(pair[index][key] for pair in group) / len(group) if group else None
    return dict(roots=len(pairs), changed=len(changed),
        mean_holes_after=[means(pairs, i, 'holes_after') for i in (0, 1)],
        mean_roof_after=[means(pairs, i, 'roof_after') for i in (0, 1)],
        changed_mean_holes_after=[means(changed, i, 'holes_after') for i in (0, 1)],
        changed_mean_roof_after=[means(changed, i, 'roof_after') for i in (0, 1)],
        projected_dead=[sum(pair[i]['projected_dead'] for pair in pairs) for i in (0, 1)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', type=Path, help='Native raw .log files')
    parser.add_argument('--binary', type=Path, default=Path('build/bot_chatgpt_root_probe'))
    parser.add_argument('--iterations', type=int, default=150)
    parser.add_argument('--stride', type=int, default=3)
    parser.add_argument('--max-roots', type=int, default=1000)
    parser.add_argument('--repeat-check', action='store_true')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output already exists')
    result = dict(note='Same visible roots, fresh trees, fixed full-overload expansion passes (completed trees may no-op). Not equal milliseconds/nodes or a replay of the original warm timed search. Projected outcomes are NOT actual wins.',
                  parameters=vars(args).copy(), runs={}, source_sha256={})
    result['parameters'] = {k: str(v) if isinstance(v, Path) else [str(p) for p in v] if isinstance(v, list) else v
                            for k, v in result['parameters'].items()}
    sources = [args.binary, Path(__file__), *Path('src/bot/chatgpt').glob('*.h'),
               Path('src/bot/chatgpt/root_probe.cpp'), Path('src/bot/native_host.h'),
               Path('zzztoj_standalone/upstream/src/tetris_core.h'),
               Path('zzztoj_standalone/upstream/src/ai_zzz.cpp'),
               Path('zzztoj_standalone/src/rule_tetr_srs_plus.cpp')]
    for path in sources + args.inputs:
        result['source_sha256'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    for path in args.inputs:
        command = [str(args.binary.resolve()), str(path), str(args.iterations), str(args.stride), str(args.max_roots)]
        output = subprocess.check_output(command, text=True)
        if args.repeat_check and output != subprocess.check_output(command, text=True):
            raise RuntimeError('Fixed-work replay was not deterministic: ' + str(path))
        rows = [json.loads(line) for line in output.splitlines()]
        result['runs'][str(path)] = dict(summary=summarize(rows), pairs=rows)
        print(path, result['runs'][str(path)]['summary'], flush=True)
    with args.output.open('x', encoding='utf-8') as out:
        json.dump(result, out, ensure_ascii=False, indent=2)
        out.write('\n')


if __name__ == '__main__':
    main()
