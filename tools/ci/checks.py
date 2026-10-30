#!/usr/bin/env python3
"""Small, dependency-free CI policy checks; no GitHub credentials required."""
import argparse
import json
import os
from pathlib import Path
import re


def conventional_title(title):
    return bool(re.fullmatch(
        r"(?:feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)"
        r"(?:\([^()\r\n]+\))?!?: [^\s].*", title
    )) and not any(c in title for c in "\r\n")


def build_matrix(data):
    if data['schema_version'] != 1 or data['architecture'] != 'x86_64':
        raise ValueError('Unsupported platform schema or architecture')
    known = {'arch-x86_64': 'arch', 'fedora44-x86_64': 'fedora'}
    rows = []
    seen = set()
    for platform in data['platforms']:
        target = platform['id']
        if target in seen:
            raise ValueError('Duplicate platform id')
        seen.add(target)
        if platform['release_required'] and not platform['selected_for_future_qualification']:
            raise ValueError('Mandatory target omitted from CI')
        if not platform['selected_for_future_qualification']:
            continue
        image = platform.get('ci_image', '')
        if target not in known or not re.fullmatch(
            r'(?:docker.io/library/archlinux|docker.io/library/fedora)@sha256:[0-9a-f]{64}', image
        ):
            raise ValueError('Unknown target or unpinned image')
        expected_image = 'archlinux' if known[target] == 'arch' else 'fedora'
        if not image.startswith(f'docker.io/library/{expected_image}@'):
            raise ValueError('Target/image disagreement')
        if image.split('@')[1] != platform['image_digest']:
            raise ValueError('Image digest disagreement')
        rows.append({'target': target, 'distribution': known[target], 'image': image})
    if not rows:
        raise ValueError('Empty CI candidate matrix')
    return {'include': rows}


def check_version(root):
    version = (root / 'version.txt').read_bytes().decode('ascii')
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\n', version):
        raise ValueError('Invalid version.txt')
    manifest = root / '.release-please-manifest.json'
    if manifest.exists() and json.loads(manifest.read_text()).get('.') != version.strip():
        raise ValueError('Release-please manifest/version mismatch')
    tag = os.environ.get('GITHUB_REF', '')
    if tag.startswith('refs/tags/') and tag != 'refs/tags/v' + version.strip():
        raise ValueError('Tag/version mismatch')


def aggregate(results):
    return set(results) == {'validation', 'builds'} and all(
        job.get('result') == 'success' for job in results.values()
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['validate', 'aggregate', 'title-hint'])
    args = parser.parse_args()
    if args.command == 'aggregate':
        if not aggregate(json.loads(os.environ['NEEDS_JSON'])):
            raise SystemExit('Required CI job failed, was cancelled, skipped, or missing')
        return
    if args.command == 'title-hint':
        if os.environ.get('GITHUB_EVENT_NAME') == 'pull_request':
            event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
            if not conventional_title(event['pull_request']['title']):
                print('::notice title=Release note hint::Maintainer: choose a Conventional Commit '
                      'message when squash-merging for release notes. '
                      'Contributors do not need to rename this PR.')
        return
    root = Path(__file__).resolve().parents[2]
    check_version(root)
    matrix = json.dumps(build_matrix(json.loads((root / 'packaging/platforms.json').read_text())))
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
            output.write(f'matrix={matrix}\n')
    print(matrix)


if __name__ == '__main__':
    main()
