from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from package_frontends import package_stage, refresh_source_metadata
from check_frontend_distribution import extract, verify


class FrontendDistributionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='termino-package-test-')
        self.base = Path(self.temporary.name)
        self.stage = self.base / 'stage'; self.stage.mkdir()
        self.output = self.base / 'output'
        self.info = dict(schema=1, frontend='terminal', platform='Linux', architecture='x64',
                         source_commit='a' * 40, source_dirty=False)
        for name, value in {
            'tetr': b'\x7fELFtest', 'tetr_ft7': b'\x7fELFtest', 'Play.sh': b'#!/bin/sh\n',
            'README.txt': b'Initial defaults', 'LICENSE': b'license', 'THIRD_PARTY_NOTICES.md': b'notice',
            'licenses/websocket/curl.txt': b'license',
        }.items():
            p = self.stage / name; p.parent.mkdir(parents=True, exist_ok=True);p.write_bytes(value)
        self.metadata()

    def tearDown(self):
        self.temporary.cleanup()

    def metadata(self):
        (self.stage / 'BUILD.json').write_text(json.dumps(self.info), encoding='utf-8')

    def test_clean_package_round_trip_and_manifest(self):
        (self.stage / '.tetr/replay').mkdir(parents=True)
        archive = package_stage(self.stage, self.output, 'local-test')
        destination = self.base / 'extracted';destination.mkdir()
        info = verify(extract(archive, destination))
        self.assertEqual(info['source_commit'], 'a' * 40)
        self.assertEqual(info['status'], 'local-candidate')
        self.assertTrue(Path(str(archive) + '.sha256').is_file())

    def test_personal_data_never_archived(self):
        p = self.stage / '.tetr/player1_linux.ini';p.parent.mkdir();p.write_text('personal')
        with self.assertRaises(ValueError):package_stage(self.stage, self.output, 'test')
        self.assertEqual(p.read_text(), 'personal')

    def test_dirty_snapshot_requires_explicit_candidate_flag(self):
        self.info['source_dirty'] = True;self.metadata()
        with self.assertRaises(ValueError):package_stage(self.stage, self.output, 'test')
        self.assertTrue(package_stage(self.stage, self.output, 'test', allow_dirty=True).is_file())

    def test_existing_artifact_is_preserved(self):
        archive = package_stage(self.stage, self.output, 'test');original = archive.read_bytes()
        with self.assertRaises(FileExistsError):package_stage(self.stage, self.output, 'test')
        self.assertEqual(archive.read_bytes(), original)

    def test_modified_executable_is_detected(self):
        package_stage(self.stage, self.output, 'test');(self.stage / 'tetr').write_bytes(b'changed')
        with self.assertRaises(ValueError):verify(self.stage)

    def test_undeclared_file_is_detected(self):
        package_stage(self.stage, self.output, 'test');(self.stage / 'unknown.txt').write_text('extra')
        with self.assertRaises(ValueError):verify(self.stage)

    def test_metadata_mismatch_is_detected(self):
        package_stage(self.stage, self.output, 'test');self.info['source_commit'] = 'b' * 40;self.metadata()
        with self.assertRaises(ValueError):verify(self.stage)

    def test_path_traversal_is_rejected(self):
        archive = self.base / 'unsafe.zip'
        with zipfile.ZipFile(archive, 'w') as package:package.writestr('../outside.txt', 'bad')
        destination = self.base / 'extracted';destination.mkdir()
        with self.assertRaises(ValueError):extract(archive, destination)
        self.assertFalse((self.base / 'outside.txt').exists())

    def test_missing_ft7_cannot_silently_reduce_terminal_package(self):
        (self.stage / 'tetr_ft7').unlink()
        with self.assertRaises(ValueError):package_stage(self.stage, self.output, 'test')

    def test_dirty_worktree_cannot_reuse_clean_configure_metadata(self):
        with patch('package_frontends.subprocess.check_output', side_effect=['a' * 40, b' M src/example.cpp']):
            refresh_source_metadata(self.stage)
        self.assertTrue(json.loads((self.stage / 'BUILD.json').read_text())['source_dirty'])
        with self.assertRaises(ValueError):package_stage(self.stage, self.output, 'test')

    def test_another_commit_requires_reconfigure(self):
        with patch('package_frontends.subprocess.check_output', return_value='b' * 40):
            with self.assertRaises(ValueError):refresh_source_metadata(self.stage)


if __name__ == '__main__':
    unittest.main()
