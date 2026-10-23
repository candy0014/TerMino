from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from package_frontends import package_stage, refresh_source_metadata, source_metadata
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

    def default_libraries(self):
        self.info['default_libraries'] = True;self.metadata()
        for name in ('maps.txt', 'presets.txt'):
            target = self.stage / '.tetr' / name;target.parent.mkdir(exist_ok=True)
            target.write_bytes((TOOLS.parent / 'resources/defaults' / name).read_bytes())

    def test_approved_default_libraries_are_preserved(self):
        self.default_libraries()
        archive = package_stage(self.stage, self.output, 'defaults')
        destination = self.base / 'extracted';destination.mkdir()
        folder = extract(archive, destination)
        self.assertTrue(verify(folder)['default_libraries'])
        for name in ('maps.txt', 'presets.txt'):
            self.assertEqual((folder / '.tetr' / name).read_bytes(),
                             (TOOLS.parent / 'resources/defaults' / name).read_bytes())

    def test_personal_library_cannot_replace_approved_defaults(self):
        self.default_libraries()
        for name in ('maps.txt', 'presets.txt'):
            with self.subTest(name=name):
                target = self.stage / '.tetr' / name;original = target.read_bytes()
                target.write_bytes(original + b'personal change')
                with self.assertRaises(ValueError):package_stage(self.stage, self.output, 'personal')
                target.write_bytes(original)

    def test_declared_default_library_cannot_be_omitted(self):
        self.default_libraries();(self.stage / '.tetr/maps.txt').unlink()
        with self.assertRaises(ValueError):package_stage(self.stage, self.output, 'missing-defaults')

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

    def bundle(self, windows=False):
        self.info.update(frontend='bundle', platform='Windows' if windows else 'Linux')
        self.metadata()
        suffix = '.exe' if windows else ''
        for name in ('tetr', 'tetr_ft7', 'tetr_gl', 'tetr_server'):
            (self.stage / (name + suffix)).write_bytes(b'MZtest' if windows else b'\x7fELFtest')
        for name in (('Play.bat', 'PlayGL.bat', 'Server.bat') if windows else ('PlayGL.sh', 'Server.sh')):
            (self.stage / name).write_text('launcher')
        for name in ('resources/fonts/NotoSansCJKsc-Regular.otf', 'resources/fonts/LICENSE-Noto.txt',
                     'resources/licenses/SDL3.txt', 'resources/licenses/FreeType-FTL.txt', 'docs/OPENGL_PREVIEW.md'):
            p = self.stage / name;p.parent.mkdir(parents=True, exist_ok=True);p.write_text('resource')

    def test_combined_linux_and_windows_round_trip(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                self.bundle(windows)
                archive = package_stage(self.stage, self.output, 'bundle-test')
                destination = self.base / str(windows);destination.mkdir()
                info = verify(extract(archive, destination))
                self.assertEqual(info['frontend'], 'bundle')
                self.assertEqual(archive.suffix, '.zip' if windows else '.gz')

    def test_bundle_cannot_omit_either_client_server_or_ft7(self):
        for name in ('tetr', 'tetr_gl', 'tetr_server', 'tetr_ft7', 'PlayGL.sh', 'Server.sh'):
            with self.subTest(missing=name):
                self.bundle();(self.stage / name).unlink()
                with self.assertRaisesRegex(ValueError, 'Missing package files'):
                    package_stage(self.stage, self.output, 'incomplete')

    def test_bundle_checks_server_executable_platform(self):
        self.bundle();(self.stage / 'tetr_server').write_bytes(b'MZwrong-platform')
        with self.assertRaisesRegex(ValueError, 'tetr_server'):
            package_stage(self.stage, self.output, 'wrong-platform')

    def test_bundle_rejects_settings_recordings_and_scores_without_deleting_them(self):
        self.bundle()
        for name in ('.tetr/config.json', '.tetr/graphics.ini', '.tetr/replay/personal.txt', '.tetr/maps.txt',
                     '.tetr/presets.txt', '.tetr/player1_linux.ini.challenges', 'keybinding_linux.txt'):
            with self.subTest(personal=name):
                file = self.stage / name;file.parent.mkdir(parents=True, exist_ok=True);file.write_bytes(b'personal')
                with self.assertRaises(ValueError):package_stage(self.stage, self.output, 'unclean')
                self.assertEqual(file.read_bytes(), b'personal');file.unlink()

    def test_cross_build_revision_never_claims_a_clean_source(self):
        with patch('package_frontends.subprocess.check_output', side_effect=FileNotFoundError), \
             patch.dict(os.environ, {'TETR_DISTRIBUTION_COMMIT': 'c' * 40, 'TETR_DISTRIBUTION_DIRTY': 'false'}):
            self.assertEqual(source_metadata(), ('c' * 40, True))

    def test_real_resource_install_has_both_launchers_and_approved_default_libraries(self):
        source = TOOLS.parent.as_posix()
        data = self.base / 'personal';(data / '.tetr/replay').mkdir(parents=True)
        personal = data / '.tetr/player1_linux.ini';personal.write_text('do not copy or reset')
        for system in ('Linux', 'Windows'):
            destination = self.base / system
            driver = self.base / 'install.cmake'
            script = self.base / 'resources.cmake'
            driver.write_text(f'''set(PROJECT_SOURCE_DIR "{source}")
set(CMAKE_CURRENT_BINARY_DIR "{self.base.as_posix()}")
set(TETR_CLIENT_FRONTEND bundle)
set(TETR_BUILD_SERVER ON)
set(CMAKE_SYSTEM_NAME {system})
set(TETR_PORTABLE_DATA_DIR "{data.as_posix()}")
configure_file("{source}/cmake/client/Install.cmake.in" "{script.as_posix()}" @ONLY)
set(CMAKE_INSTALL_PREFIX "{destination.as_posix()}")
include("{script.as_posix()}")
''')
            subprocess.run(['cmake', '-P', str(driver)], check=True, stdout=subprocess.DEVNULL)
            suffix = 'bat' if system == 'Windows' else 'sh'
            self.assertIn('tetr_gl', (destination / ('PlayGL.' + suffix)).read_text())
            self.assertNotIn('tetr_gl', (destination / ('Play.' + suffix)).read_text())
            self.assertTrue((destination / ('Server.' + suffix)).is_file())
            self.assertTrue((destination / '.tetr/replay').is_dir())
            self.assertEqual(list((destination / '.tetr/replay').iterdir()), [])
            self.assertEqual({p.name for p in (destination / '.tetr').rglob('*') if p.is_file()},
                             {'maps.txt', 'presets.txt'})
            for name in ('maps.txt', 'presets.txt'):
                self.assertEqual((destination / '.tetr' / name).read_bytes(),
                                 (TOOLS.parent / 'resources/defaults' / name).read_bytes())
        self.assertEqual(personal.read_text(), 'do not copy or reset')


if __name__ == '__main__':
    unittest.main()
