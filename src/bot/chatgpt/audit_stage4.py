#!/usr/bin/env python3
"""Read-only independent audit of retained stage-4 benchmark artifacts.

Run from the repository root. Historical source manifests are retained but
only --verify-current checks the selected frozen candidate against the current tree.
An unavailable archived binary cannot be rehashed on another computer.
"""
import argparse
import collections
import hashlib
import json
import math
import pathlib
import statistics
import unicodedata

root = pathlib.Path.cwd()
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--verify-current', action='store_true', help='Verify the selected candidate source and executable hashes too')
parser.add_argument('--candidate-prefix', default='chatgpt_stage4_acceptance_', help='Directory prefix for the frozen candidate to verify')
parser.add_argument('--candidate-suffix', default='_v04')
parser.add_argument('--runs-prefix', default='chatgpt_stage4_', help='Artifact directory prefix to audit (default retains stage-4 scope)')
parser.add_argument('--binary', type=pathlib.Path, default=pathlib.Path('build/tetr'))
args = parser.parse_args()
def selected_candidate(name):
    return name.startswith(args.candidate_prefix) and name.endswith(args.candidate_suffix)

games = records = placements = 0
verified = 0
search_times = []
fallbacks = []
for directory in sorted((root / 'runlogs').glob(args.runs_prefix + '*')):
    if not directory.is_dir():
        continue
    if not (directory / 'summary.csv').exists():
        print('INCOMPLETE', directory.name)
        continue
    metadata = json.loads((directory / 'metadata.json').read_text())
    assert metadata['versus_protocol'] == 'turn-pieces-v2'
    binary = pathlib.Path(metadata['binary'])
    if 'binary_source_metadata' in metadata:
        assert metadata['binary_sha256'] == metadata['binary_source_metadata']['binary_sha256']
        if binary.exists():
            assert hashlib.sha256(binary.read_bytes()).hexdigest() == metadata['binary_sha256']
        else:
            print('ARCHIVED BINARY UNAVAILABLE', binary)
    if args.verify_current and selected_candidate(directory.name):
        assert metadata['binary_sha256'] == hashlib.sha256(args.binary.read_bytes()).hexdigest()
        for name, sha in metadata['source_sha256'].items():
            assert hashlib.sha256((root / name).read_bytes()).hexdigest() == sha, name
        verified += 1
    wins = collections.Counter(first=0, second=0)
    rows = [json.loads(line) for line in (directory / 'results.jsonl').read_text().splitlines()]
    for record in rows:
        label = record['case'] + '_seed' + str(record['seed'])
        side = record['side']
        events = json.loads((directory / (label + '.placements.json')).read_text())[side]
        assert len(events) == record['pieces']
        assert record['game_seconds'] == record['pieces']
        for i, event in enumerate(events):
            assert int(event['total_pieces']) == i + 1 and float(event['t']) == i
        for event_key, result_key in [('attack', 'attack'), ('lines', 'lines'), ('garbage_lines', 'garbage_lines')]:
            assert sum(int(e[event_key]) for e in events) == record[result_key]
        clear_types, spikes, releases, combo_lengths, combo_attacks = (collections.Counter() for _ in range(5))
        chain = []
        attack_chain = []
        special = clears = 0
        for event in events:
            lines, attack = int(event['lines']), int(event['attack'])
            spin, mini = int(event['spin']), int(event['mini'])
            if lines:
                clears += 1
                special += bool(spin or lines >= 4)
                kind = 'minispin' if spin and mini else 'spin' if spin else 'normal'
                clear_types[unicodedata.normalize('NFKC', event['piece']) + ':' + kind + ':' + str(lines)] += 1
                chain.append(attack)
                if not spin and lines < 4 and int(event['b2b_before']) > 0 and int(event['b2b_after']) == 0:
                    releases[str(int(event['b2b_charge']))] += 1
            elif chain:
                combo_lengths[str(len(chain))] += 1
                combo_attacks[str(sum(chain))] += 1
                chain = []
            if attack:
                if attack_chain and float(event['t']) - attack_chain[-1][0] > 1 + 1e-9:
                    spikes[str(sum(a for _, a in attack_chain))] += 1
                    attack_chain = []
                attack_chain.append((float(event['t']), attack))
        if chain:
            combo_lengths[str(len(chain))] += 1
            combo_attacks[str(sum(chain))] += 1
        if attack_chain:
            spikes[str(sum(a for _, a in attack_chain))] += 1
        if record['remaining_b2b']:
            releases[str(record['remaining_b2b'])] += 1
        for key, expected in [('clear_types', clear_types), ('spikes', spikes), ('combo_lengths', combo_lengths),
                              ('combo_attacks', combo_attacks), ('b2b_releases_including_final', releases)]:
            assert record[key] == expected, (directory.name, label, side, key)
        assert record['special_clear_fraction'] == (special / clears if clears else 0)
        if record['bot'] == 9:
            decisions = json.loads((directory / (label + '.decisions.json')).read_text())[side]
            assert len(decisions) == len(events)
            assert record['diagnostic_fallbacks'] == sum(int(d['fallback']) != 0 for d in decisions)
            for i, decision in enumerate(decisions):
                board = list(map(int, decision['board'].split(',')))
                assert len(board) == 40 and all(0 <= row < 1024 for row in board)
                assert int(decision['roof']) == max((j + 1 for j, row in enumerate(board) if row), default=0)
                assert int(decision['pieces']) == i
                if int(decision['fallback']):
                    fallbacks.append((directory.name, record['seed'], i, int(decision['fallback']), int(decision['roof'])))
                    # Reviewed: the archived v0.3 cannot generate its normal
                    # root above the skyline; the host's legal BFS can clutch.
                    assert directory.name.endswith('_v03')
                    assert int(decision['fallback']) == 1 and int(decision['roof']) > 20
                assert 0 <= int(decision['ready']) <= int(decision['incoming'])
                assert 0 <= float(decision.get('dig_pressure', 0)) <= 1
                if directory.name.startswith('chatgpt_stage4_stable_') and directory.name.endswith('_v031'):
                    assert float(decision['dig_pressure']) == 0, 'Experimental blend activated in maintenance release'
                if selected_candidate(directory.name):
                    search_times.append(float(decision['search_ms']))
        assert (directory / (label + '.replay.txt')).stat().st_size > 0
        if side == 'first':
            games += 1
            if record['parameters']['mode'] == 6:
                raw_sides = [line.split('side=', 1)[1].split()[0] for line in (directory / (label + '.log')).read_text().splitlines()
                             if line.startswith('TETR_TEST_PLACEMENT ')]
                expected, n = [], 0
                for turn in range(1, record['pieces'] + 2):
                    expected.append('first')
                    upto = math.ceil(turn * record['parameters']['opponent_turn_pieces'] - 1e-9)
                    expected.extend(['second'] * (upto - n))
                    n = upto
                assert raw_sides == expected[:len(raw_sides)]
        if record['outcome'] == 'won':
            wins[side] += 1
        records += 1
        placements += len(events)
    for score in directory.glob('*_score.json'):
        assert json.loads(score.read_text()) == wins
    print('PASS', directory.name)
print('TOTAL', games, 'games', records, 'player records', placements, 'placements')
print('REVIEWED FALLBACKS', fallbacks)
if args.verify_current:
    assert verified > 0, 'No completed directories matched the requested current candidate'
    print('CURRENT SOURCE/BINARY VERIFIED DIRECTORIES', verified)
if search_times:
    print('FINAL SEARCH MS avg/p95/max', statistics.mean(search_times),
          sorted(search_times)[int(.95 * (len(search_times) - 1))], max(search_times))
