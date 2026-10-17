#!/usr/bin/env python3
"""Require explicit native evidence before promoting the four candidates."""
from pathlib import Path
import argparse
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CODE_PATHS = ['src', 'include', 'cmake', 'third_party', 'resources', 'tools', 'tests', '.github',
              'CMakeLists.txt', 'CMakePresets.json', 'compile.sh', 'compile.ps1', 'compile.bat',
              'package.sh', 'package.ps1', 'package.bat', 'package_cmd.bat']


def validate(report, commit):
    source = report.get('source_commit')
    if not source:
        raise ValueError('Native acceptance is pending: no source commit recorded')
    if source != commit:
        # A later documentation-only acceptance-record commit is valid;
        # implementation/build/input/resource changes invalidate the evidence.
        result = subprocess.run(['git', 'diff', '--exit-code', source, commit, '--', *CODE_PATHS],
                                cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if result.returncode:
            raise ValueError('Native evidence refers to different implementation/build sources')
    cases = report.get('cases', [])
    expected = {(p, f) for p in ('Linux', 'Windows') for f in ('terminal', 'opengl')}
    if len(cases) != 4 or {(c.get('platform'), c.get('frontend')) for c in cases} != expected:
        raise ValueError('Native evidence must cover all four combinations')
    for case in cases:
        name = f'{case["platform"]}/{case["frontend"]}'
        if case.get('environment') != 'native' or case.get('status') != 'passed' or not case.get('evidence'):
            raise ValueError(f'{name}: native acceptance/evidence is incomplete')
        required = {'build', 'regression', 'keyboard', 'focus', 'network_replay', 'data_upgrade'}
        if case['frontend'] == 'opengl':
            required |= {'ime', 'dpi', 'gpu_driver'}
        if any(case.get('checks', {}).get(key) != 'passed' for key in required):
            raise ValueError(f'{name}: required manual/native checks are pending')
    return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path);parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    validate(json.loads(args.report.read_text(encoding='utf-8')), args.commit)
    print('Four-combination native acceptance verified.')
