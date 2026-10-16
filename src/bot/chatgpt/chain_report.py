#!/usr/bin/env python3
"""Read-only B2B retention analysis of native benchmark placements.

Full chain length is not released charge: below five a broken chain sends no
charge; above that threshold charge is one less than chain length. Final active
chains are censored, never reported as completed/broken chains. No BOT imports
this reporting module. Primary versus scores must be assessed first.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path


def chain_metrics(events):
    completed = collections.Counter()
    started = extended = broken = preserved = peak = incremented = 0
    previous = 0
    for event in events:
        before, after = int(event['b2b_before']), int(event['b2b_after'])
        lines = int(event['lines'])
        if before < 0 or after < 0 or lines < 0:
            raise ValueError('Negative chain or clear count')
        if before != previous:
            raise ValueError('Non-contiguous or truncated B2B state')
        if after > before:
            if not lines or after != before + 1:
                raise ValueError('Unsupported B2B increment')
            incremented += 1
            if before:
                extended += 1
            else:
                started += 1
        elif before and after == 0:
            if not lines:
                raise ValueError('Zero-line placement broke B2B')
            broken += 1
            completed[before] += 1
        elif after == before and not lines:
            preserved += bool(before)
        elif not (lines and before == after == 0):
            raise ValueError('Unsupported chain transition')
        peak = max(peak, after, before)
        previous = after
    return dict(started=started, extended=extended, broken=broken,
                # Includes PC increments; not the benchmark's Spin/Quad fraction.
                preserved_without_clear=preserved, incrementing_clears=incremented,
                active_clear_retention=extended / (extended + broken) if extended + broken else None,
                completed_chain_lengths=dict(sorted(completed.items())),
                final_active_chain=previous, peak_b2b=peak)


def summarize(rows):
    extended = sum(r['chain']['extended'] for r in rows)
    broken = sum(r['chain']['broken'] for r in rows)
    completed = collections.Counter()
    censored = collections.Counter()
    for row in rows:
        completed.update(row['chain']['completed_chain_lengths'])
        if row['chain']['final_active_chain']:
            censored[row['chain']['final_active_chain']] += 1
    return dict(games=len(rows), outcomes=dict(collections.Counter(r['outcome'] for r in rows)),
                pieces=sum(r['pieces'] for r in rows), extended=extended, broken=broken,
                active_clear_retention=extended / (extended + broken) if extended + broken else None,
                completed_chain_lengths=dict(sorted(completed.items())),
                censored_final_chains=dict(sorted(censored.items())),
                peak_each=[r['chain']['peak_b2b'] for r in rows])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    hashes = {str(Path(__file__)): hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    def read(path):
        raw = path.read_bytes()
        hashes[str(path)] = hashlib.sha256(raw).hexdigest()
        return json.loads(raw)
    result = dict(definition='retention = extensions / (extensions + breaks) on clears with an active chain; zero-line preservation excluded', runs={})
    for directory in args.inputs:
        raw = (directory / 'results.jsonl').read_bytes()
        hashes[str(directory / 'results.jsonl')] = hashlib.sha256(raw).hexdigest()
        metadata = read(directory / 'metadata.json')
        groups = collections.defaultdict(list)
        for line in raw.splitlines():
            r = json.loads(line)
            events = read(directory / f"{r['case']}_seed{r['seed']}.placements.json")[r['side']]
            assert len(events) == r['pieces']
            metrics = chain_metrics(events)
            assert metrics['final_active_chain'] == r['remaining_b2b']
            groups[(r['case'], r['side'])].append(dict(seed=r['seed'], bot=r['bot'],
                outcome=r['outcome'], pieces=r['pieces'], chain=metrics))
        result['runs'][str(directory)] = dict(binary_sha256=metadata['binary_sha256'], groups=[
            dict(case=case, side=side, summary=summarize(rows), games=rows)
            for (case, side), rows in sorted(groups.items())])
    result['source_sha256'] = hashes
    with args.output.open('x', encoding='utf-8') as out:
        json.dump(result, out, ensure_ascii=False, indent=2)
        out.write('\n')


if __name__ == '__main__':
    main()
