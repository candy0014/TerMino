#!/usr/bin/env python3
"""Freeze nine retrospective teacher windows before inspecting search results."""
import argparse
import hashlib
import json
from pathlib import Path

SELECTION = [
    ('pressure_1_setup', 'pressure_1', 130, 6),
    ('pressure_2_four_spins', 'pressure_2', 90, 6),
    ('pressure_2_three_spins', 'pressure_2', 102, 6),
    ('pressure_3_pressure_chain', 'pressure_3', 125, 6),
    ('pressure_3_i_spin', 'pressure_3', 126, 6),
    ('burst_1_quad_combo', 'burst_1', 81, 6),
    ('burst_2_spin_chain', 'burst_2', 189, 6),
    ('burst_2_double_quad', 'burst_2', 192, 6),
    ('burst_3_quad_combo', 'burst_3', 289, 6),
]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--input', type=Path, default=Path('runlogs/chatgpt_moch_20260926/tactical_cases_v2/pressure_inputs'))
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    records = json.loads((args.input / 'prepared.json').read_text())['records']
    indexed = {(r['case_id'], r['placement']): r for r in records}
    windows = []
    for name, case, start, length in SELECTION:
        steps = [indexed[case, i] for i in range(start, start + length)]
        root = steps[0]['before_first_action']['snapshot']
        queue = root['visible_next']
        held, cursor = root['hold'], 0
        for step in steps:
            assert step['eligible'] and step['actual_garbage_rise_rows'] == 0
            assert step['teacher_spin_observed']
            assert cursor < len(queue)
            current = queue[cursor]
            cursor += 1
            selected = current
            if step['teacher_hold']:
                assert held != current, 'same-family HOLD is not a host-encodable choice'
                if held:
                    selected = held
                else:
                    assert cursor < len(queue)
                    selected = queue[cursor]
                    cursor += 1
                held = current
            assert selected == 'ijlostz'[step['teacher_family']]
        windows.append({'window': name, 'case_id': case, 'start': start, 'length': length,
            'sample_ids': [s['sample_id'] for s in steps],
            'observed_rises': [s['actual_garbage_rise_rows'] for s in steps],
            'action_frames': [s['action_frame'] for s in steps],
            'lock_frames': [s['teacher_lock_frame'] for s in steps],
            'root': steps[0]['before_first_action'],
            'teacher': [{key: s[key] for key in ('placement', 'teacher_family', 'teacher_hold',
                'teacher_lines', 'teacher_spin', 'teacher_mini', 'teacher_b2b_after',
                'teacher_combo_after', 'teacher_delta', 'teacher_action_delay_seconds')} for s in steps]})
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {'selection_basis': 'Nine fixed strong-play examples selected from teacher data before new search outcomes; overlapping windows are case studies, not independent statistics.',
        'limits': 'Only first root current+5 and root pressure feed search. Subsequent target labels do not supply future queue, pressure or garbage holes. Stop on predicted or observed garbage rise.',
        'input_hashes': {name: digest(args.input / name) for name in ('prepared.json', 'roots.log')},
        'windows': windows}
    (args.output / 'windows.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (args.output / 'windows.log').write_text(''.join(
        f"window={w['window']} sample_ids={','.join(map(str, w['sample_ids']))} observed_rises={','.join(map(str, w['observed_rises']))}\n" for w in windows))
    print(json.dumps([{'window': w['window'], 'sample_ids': w['sample_ids']} for w in windows]))

if __name__ == '__main__':
    main()
