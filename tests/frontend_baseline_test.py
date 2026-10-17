"""Keep the complete terminal release's public identifiers during migration."""
from pathlib import Path
import json
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASELINE = json.loads((ROOT / 'docs/TERMINAL_BASELINE.json').read_text(encoding='utf-8'))


def enum_names(path, name):
    text = (ROOT / path).read_text(encoding='utf-8')
    body = re.search(r'enum class ' + name + r'[^\{]*\{(.*?)\};', text, re.S).group(1)
    body = re.sub(r'//[^\n]*', '', body)
    return {part.strip().split('=')[0].strip() for part in body.split(',') if part.strip()}


class TerminalBaselineTest(unittest.TestCase):
    def test_all_settings_actions_and_modes_retained(self):
        for key, path, enum in [
            ('settings', 'include/tetr/config/settings_schema.h', 'SettingId'),
            ('actions', 'include/tetr/input/action.h', 'Action'),
            ('game_modes', 'include/tetr/runtime/game_modes.h', 'GameMode'),
        ]:
            self.assertFalse(set(BASELINE[key]) - enum_names(path, enum), key)

    def test_command_flags_and_executables_retained(self):
        for path, flags in BASELINE['cli_flags'].items():
            current = set(re.findall(r'"(--[A-Za-z][A-Za-z0-9-]*)"', (ROOT / path).read_text(encoding='utf-8')))
            self.assertFalse(set(flags) - current, path)
        cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        for target in BASELINE['executables']:
            self.assertRegex(cmake, r'add_executable\(' + target + r'\s')

    def test_challenge_catalog_retained(self):
        catalog = (ROOT / 'docs/SINGLE_PLAYER_CHALLENGE_CATALOG.txt').read_text(encoding='utf-8')
        current = set(re.findall(r'\[([a-z0-9][a-z0-9-]+)\]', catalog))
        self.assertFalse(set(BASELINE['challenges']) - current)


def check_registered_challenges(executable):
    # The native catalog comes from the actual registry, independently of docs.
    output = subprocess.check_output([executable, '--catalog'], cwd=ROOT, text=True, encoding='utf-8')
    ids = {line.split('\t')[1] for line in output.splitlines() if '\t' in line}
    missing = set(BASELINE['challenges']) - ids
    if missing:
        raise AssertionError(f'Baseline challenges removed from the registry: {sorted(missing)}')
    print(f'Terminal baseline: all {len(BASELINE["challenges"])} fixed challenges retained.')


if __name__ == '__main__':
    if len(sys.argv) == 2:
        check_registered_challenges(sys.argv[1])
    else:
        unittest.main()
