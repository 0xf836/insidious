"""Source identity, integrity and path safety at the packaging boundary."""
import hashlib
import configparser
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import subprocess
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('prepare', ROOT / 'tools/packaging/prepare.py')
prepare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prepare)


class PackagingTests(unittest.TestCase):
    def test_runtime_diagnostic_failures_stop_the_runner(self):
        helper = ROOT / 'tools/packaging/assertions.sh'
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'launch.log'
            for content, passed in [('startup complete', True),
                                    ('libQt6Gui.so => not found', False),
                                    ('Could not connect to display', False),
                                    (None, False)]:
                with self.subTest(content=content):
                    if content is not None:
                        log.write_text(content)
                    else:
                        log.unlink()
                    result = subprocess.run(
                        ['bash', '-c', 'set -euo pipefail; source "$1"; '
                         'reject_matches "not found|could not connect" "$2"; '
                         'printf reached-success', '_', str(helper), str(log)],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode == 0, passed)
                    self.assertEqual('reached-success' in result.stdout, passed)

    def test_desktop_resource_identity_agrees(self):
        metadata = ET.parse(ROOT / 'resources/io.github._0xf836.Insidious.metainfo.xml').getroot()
        identity = metadata.findtext('id')
        self.assertGreaterEqual(len(identity.split('.')), 3)
        for component in identity.split('.'):
            self.assertRegex(component, r'^[A-Za-z_][A-Za-z0-9_]*$')
        desktop = metadata.findtext("launchable[@type='desktop-id']")
        self.assertEqual(desktop, identity + '.desktop')
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(ROOT / 'resources' / desktop)
        entry = parser['Desktop Entry']
        self.assertEqual(entry['Icon'], identity)
        self.assertEqual(entry['Exec'], 'insidious %f')
        for directory in ['scalable', '48x48', '64x64', '128x128', '256x256']:
            suffix = 'svg' if directory == 'scalable' else 'png'
            self.assertTrue((ROOT / 'resources/icons' / directory / 'apps' / f'{identity}.{suffix}').is_file())

    def archive(self, directory, changes=None, extra=None):
        files = {'version.txt': '0.1.0\n',
                 'source-metadata.json': json.dumps({'version': '0.1.0', 'commit': 'a' * 40})}
        files.update(changes or {})
        source = directory / 'insidious-0.1.0.tar.xz'
        with tarfile.open(source, 'w:xz') as archive:
            for name, content in files.items():
                member = tarfile.TarInfo(f'insidious-0.1.0/{name}')
                data = content.encode()
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
            if extra:
                archive.addfile(extra)
        return source

    def test_prepared_recipes_bind_version_and_source_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            source = self.archive(directory)
            output = directory / 'recipes'
            prepare.prepare(source, output)
            checksum = hashlib.sha256(source.read_bytes()).hexdigest()
            self.assertEqual((output / source.name).read_bytes(), source.read_bytes())
            self.assertEqual((output / 'SHA256SUMS').read_text(), f'{checksum}  {source.name}\n')
            arch = (output / 'PKGBUILD').read_text()
            rpm = (output / 'insidious.spec').read_text()
            self.assertIn(f"sha256sums=('{checksum}')", arch)
            self.assertIn('pkgver=0.1.0\n', arch)
            self.assertIn('Version:        0.1.0\n', rpm)
            self.assertNotIn('SKIP', arch)
            with self.assertRaises(FileExistsError):
                prepare.prepare(source, output)

    def test_reject_identity_mismatches_before_creating_output(self):
        for changes in [{'version.txt': '0.2.0\n'}, {'version.txt': '0.1.0'},
                        {'source-metadata.json': '{}'},
                        {'source-metadata.json': '[]'},
                        {'source-metadata.json': '{'},
                        {'source-metadata.json': json.dumps({'version': '0.2.0', 'commit': 'a' * 40})}]:
            with self.subTest(changes=changes), tempfile.TemporaryDirectory() as directory:
                directory = Path(directory)
                source = self.archive(directory, changes)
                output = directory / 'recipes'
                with self.assertRaises(ValueError):
                    prepare.prepare(source, output)
                self.assertFalse(output.exists())

    def test_reject_unsafe_members(self):
        for name, kind in [('insidious-0.1.0/../escape', tarfile.REGTYPE),
                           ('/absolute', tarfile.REGTYPE),
                           ('wrong-root/file', tarfile.REGTYPE),
                           ('insidious-0.1.0/.git/config', tarfile.REGTYPE),
                           ('insidious-0.1.0/link', tarfile.SYMTYPE),
                           ('insidious-0.1.0/version.txt', tarfile.REGTYPE)]:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                member = tarfile.TarInfo(name)
                member.type = kind
                member.linkname = '/outside'
                source = self.archive(Path(directory), extra=member)
                with self.assertRaises(ValueError):
                    prepare.inspect_source(source)


if __name__ == '__main__':
    unittest.main()
