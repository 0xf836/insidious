"""Source export regressions; GitHub publication uses the standard CLI."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('artifacts', ROOT / 'tools/release/artifacts.py')
artifacts = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(artifacts)


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.command('init', '-q', '--initial-branch=master')
        self.command('config', 'user.name', 'Test')
        self.command('config', 'user.email', 'test@example.invalid')
        (self.source / 'version.txt').write_text('0.1.0\n')
        (self.source / '.release-please-manifest.json').write_text('{".": "0.1.0"}\n')
        (self.source / 'image.txt').write_text('fixture\n')
        self.command('add', '.')
        self.command('commit', '-qm', 'feat: viewer')
        self.commit = self.command('rev-parse', 'HEAD').strip()
        self.command('tag', 'v0.1.0')

    def command(self, *args):
        return subprocess.check_output(['git', '-C', str(self.source), *args],
                                       stderr=subprocess.PIPE).decode()

    def test_source_is_reproducible_and_has_normalized_identity(self):
        first = artifacts.source_archive(self.source, self.root / 'one', 'v0.1.0')
        second = artifacts.source_archive(self.source, self.root / 'two', 'v0.1.0')
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with tarfile.open(first) as archive:
            metadata = json.load(archive.extractfile('insidious-0.1.0/source-metadata.json'))
            self.assertEqual(metadata, {'version': '0.1.0', 'commit': self.commit})
            for item in archive:
                self.assertEqual((item.uid, item.gid, item.uname, item.gname), (0, 0, '', ''))
                self.assertEqual(item.mtime, int(self.command('show', '-s', '--format=%ct', 'HEAD')))
                self.assertNotIn('.git', Path(item.name).parts)

    def test_source_rejects_wrong_tag_dirty_and_untracked_state(self):
        with self.assertRaises(ValueError):
            artifacts.source_archive(self.source, self.root / 'wrong', 'v0.2.0')
        (self.source / 'untracked').touch()
        with self.assertRaises(ValueError):
            artifacts.source_archive(self.source, self.root / 'untracked', 'v0.1.0')
        (self.source / 'untracked').unlink()
        (self.source / 'version.txt').write_text('0.2.0\n')
        with self.assertRaises(ValueError):
            artifacts.source_archive(self.source, self.root / 'dirty', 'v0.1.0')

    def test_candidate_has_no_tag_requirement_and_cannot_claim_a_release_tag(self):
        self.command('tag', '-d', 'v0.1.0')
        path = artifacts.source_archive(self.source, self.root / 'candidate', candidate=True)
        self.assertTrue(path.exists())
        with self.assertRaises(ValueError):
            artifacts.source_archive(self.source, self.root / 'bad-candidate', 'v0.1.0', True)

    def test_rust_sources_are_prepared_from_export_and_normalized(self):
        bridge = self.source / 'rust/jxl_bridge'
        bridge.mkdir(parents=True)
        (bridge / 'Cargo.toml').write_text('[package]\nname="fixture"\nversion="0.1.0"\n')
        self.command('add', '.')
        self.command('commit', '-qm', 'test: Rust bridge')
        def vendor(manifest):
            (manifest.parent / 'vendor/sample').mkdir(parents=True)
            (manifest.parent / 'vendor/sample/lib.rs').write_text('// source\n')
            (manifest.parent / '.cargo').mkdir()
            (manifest.parent / '.cargo/config.toml').write_text('directory="vendor"\n')
            (manifest.parent / 'third-party').mkdir()
            (manifest.parent / 'third-party/THIRD_PARTY.json').write_text('[]\n')
            (manifest.parent / 'bundled-rust.provides').write_text('')
        with patch.object(artifacts, 'vendor', side_effect=vendor) as prepare:
            path = artifacts.source_archive(self.source, self.root / 'rust-source', candidate=True)
            prepare.assert_called_once()
        self.assertFalse((bridge / 'vendor').exists())
        with tarfile.open(path) as archive:
            member = archive.getmember('insidious-0.1.0/rust/jxl_bridge/vendor/sample/lib.rs')
            self.assertEqual((member.uid, member.gid, member.uname, member.gname), (0, 0, '', ''))
            self.assertEqual(archive.extractfile(member).read(), b'// source\n')

    def test_source_rejects_symlinks(self):
        (self.source / 'link').symlink_to('image.txt')
        self.command('add', '.')
        self.command('commit', '-qm', 'test: symlink')
        with self.assertRaises(ValueError):
            artifacts.source_archive(self.source, self.root / 'bad-source', candidate=True)


if __name__ == '__main__':
    unittest.main()
