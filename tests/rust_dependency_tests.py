"""Locked Rust source and license preparation without registry access."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('rust_dependencies', Path(__file__).resolve().parents[1] / 'tools/rust_dependencies.py')
rust = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rust)


class RustDependencyTests(unittest.TestCase):
    def test_notices_are_deterministic_and_do_not_expose_source_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'LICENSE-MIT').write_text('license fixture')
            package = {'name': 'sample', 'version': '1.2.3', 'license': 'MIT',
                       'manifest_path': str(root / 'Cargo.toml'), 'source': 'registry', 'repository': 'https://example.invalid/sample'}
            with patch.object(rust, 'cargo', return_value=json.dumps({'packages': [package]})):
                rust.notices(root / 'Cargo.toml', root / 'notices')
            inventory = (root / 'notices/THIRD_PARTY.json').read_text()
            self.assertNotIn(str(root), inventory)
            self.assertEqual(json.loads(inventory), [{'name': 'sample', 'version': '1.2.3', 'license': 'MIT'}])
            self.assertEqual((root / 'notices/sample-1.2.3/LICENSE-MIT').read_text(), 'license fixture')

    def test_missing_license_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = {'name': 'sample', 'version': '1', 'license': 'MIT', 'source': 'registry',
                       'repository': None, 'manifest_path': str(root / 'Cargo.toml')}
            with patch.object(rust, 'cargo', return_value=json.dumps({'packages': [package]})):
                with self.assertRaises(ValueError):
                    rust.notices(root / 'Cargo.toml', root / 'notices')
