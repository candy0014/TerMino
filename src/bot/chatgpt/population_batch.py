#!/usr/bin/env python3
"""Run a preregistered finite batch sequentially; never imported by a BOT.

No adaptive stopping or parameter changes. --start-job resumes only after all
earlier jobs completed; an interrupted output needs manual inspection rather
than silent overwrite. The manifest holds exact binary provenance and seeds.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def completed(job):
    directory = Path(job['output'])
    rows = [json.loads(s) for s in (directory / 'results.jsonl').read_text().splitlines()]
    first = [r for r in rows if r['side'] == 'first']
    if not first or any(r['case'] != job['case'] for r in rows):
        raise ValueError('Missing or wrong case records')
    metadata = json.loads((directory / 'metadata.json').read_text())
    if job['suite'] == 'versus':
        score = json.loads((directory / (job['case'] + '_score.json')).read_text())
        games = job.get('games', metadata.get('versus_games'))
        if games is not None:
            if len(first) != games or metadata.get('versus_games') != games:
                raise ValueError('Incomplete or mismatched fixed-game batch')
            if score.get('games') != games or score['first'] + score['second'] + score.get('draws', 0) != games:
                raise ValueError('Fixed-game score count mismatch')
        elif max(score['first'], score['second']) != job.get('first_to', 3):
            raise ValueError('Incomplete FT3 series')
    elif len(first) != job['repetitions']:
        raise ValueError('Incomplete solo repetitions')
    if metadata['binary_sha256'] != job['binary_sha256']:
        raise ValueError('Wrong archived binary')
    if 'cpu_affinity' in job and metadata.get('cpu_affinity') != sorted(job['cpu_affinity']):
        raise ValueError('Wrong CPU affinity')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--start-job', type=int, default=0)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    jobs = manifest['jobs']
    if not 0 <= args.start_job < len(jobs):
        parser.error('start-job out of range')
    for job in jobs:
        if hashlib.sha256(Path(job['binary']).read_bytes()).hexdigest() != job['binary_sha256']:
            raise ValueError('Frozen binary hash mismatch')
    for job in jobs[:args.start_job]:
        completed(job)
    if any(Path(j['output']).exists() for j in jobs[args.start_job:]):
        raise ValueError('Output already exists; do not overwrite or silently skip a partial job')
    runner = Path(__file__).with_name('benchmark.py')
    for index in range(args.start_job, len(jobs)):
        job = jobs[index]
        print(f"JOB {index + 1}/{len(jobs)} {job['variant']} {job['case']} seed={job['seed']}", flush=True)
        command = [sys.executable, str(runner), '--binary', job['binary'],
            '--binary-source-metadata', job['metadata'], '--suite', job['suite'],
            '--case', job['case'], '--width', '300', '--pieces', '1000',
            '--repetitions', str(job['repetitions']), '--seed', str(job['seed']),
            '--timeout', str(job.get('timeout', 1800)),
            '--save-replays', '--output', job['output']]
        if job['suite'] == 'versus':
            if 'first_to' in job:
                command += ['--first-to', str(job['first_to'])]
            else:
                command += ['--versus-games', str(job.get('games', 10))]
        if 'cpu_affinity' in job:
            cpus = job['cpu_affinity']
            if not cpus or any(type(cpu) is not int or cpu < 0 for cpu in cpus):
                raise ValueError('cpu_affinity must contain nonnegative integer CPU ids')
            command = ['taskset', '--cpu-list', ','.join(map(str, cpus)), *command]
        subprocess.run(command, check=True)
        completed(job)
        print(f"COMPLETE {index + 1}/{len(jobs)} {job['output']}", flush=True)


if __name__ == '__main__':
    main()
