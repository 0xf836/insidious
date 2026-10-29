"""CI policy regressions: fail closed on missing targets, jobs and identity."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('checks', ROOT / 'tools/ci/checks.py')
checks = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checks)


class PolicyTests(unittest.TestCase):
    def setUp(self):
        self.platforms = json.loads((ROOT / 'packaging/platforms.json').read_text())

    def test_titles(self):
        for title in ['fix: restore zoom', 'feat(viewer)!: change zoom policy', 'ci: add builds', 'chore(master): release 0.1.0']:
            self.assertTrue(checks.conventional_title(title), title)
        for title in ['Fix zoom', 'fix: ', 'fix:zoom', 'feat(): zoom', 'fix: zoom\nextra', 'fix: zoom\n']:
            self.assertFalse(checks.conventional_title(title), title)

    def test_matrix(self):
        rows = checks.build_matrix(self.platforms)['include']
        self.assertEqual({r['target'] for r in rows}, {'arch-x86_64', 'fedora44-x86_64'})

    def test_bad_matrices(self):
        for mutation in ['empty', 'unpinned', 'duplicate', 'missing-mandatory', 'wrong-image', 'digest']:
            data = copy.deepcopy(self.platforms)
            if mutation == 'empty':
                data['platforms'] = []
            elif mutation == 'unpinned':
                data['platforms'][0]['ci_image'] = 'archlinux:base'
            elif mutation == 'duplicate':
                data['platforms'].append(data['platforms'][0])
            elif mutation == 'missing-mandatory':
                data['platforms'][0].update(release_required=True, selected_for_future_qualification=False)
            elif mutation == 'wrong-image':
                data['platforms'][0]['ci_image'] = data['platforms'][1]['ci_image']
            else:
                data['platforms'][0]['image_digest'] = 'sha256:' + '0' * 64
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                checks.build_matrix(data)

    def test_aggregate(self):
        passed = {name: {'result': 'success'} for name in ['validation', 'builds']}
        self.assertTrue(checks.aggregate(passed))
        self.assertFalse(checks.aggregate({}))
        self.assertFalse(checks.aggregate({'validation': {'result': 'success'}}))
        for job in passed:
            for status in ['failure', 'cancelled', 'skipped', '', None]:
                results = copy.deepcopy(passed)
                results[job]['result'] = status
                self.assertFalse(checks.aggregate(results), (job, status))

    def test_version(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {'GITHUB_REF': 'refs/tags/v0.1.0'}):
            root = Path(directory)
            (root / 'version.txt').write_text('0.1.0\n')
            checks.check_version(root)
            (root / '.release-please-manifest.json').write_text('{".": "0.2.0"}')
            with self.assertRaises(ValueError):
                checks.check_version(root)
            (root / '.release-please-manifest.json').write_text('{".": "0.1.0"}')
            with patch.dict(os.environ, {'GITHUB_REF': 'refs/tags/v0.2.0'}), self.assertRaises(ValueError):
                checks.check_version(root)
            for invalid in ['0.1.0', '01.1.0\n', '0.1.0\n\n', '0.1.0\r\n']:
                (root / 'version.txt').write_text(invalid)
                with self.assertRaises(ValueError):
                    checks.check_version(root)


if __name__ == '__main__':
    unittest.main()
