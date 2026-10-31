#!/usr/bin/env python3
"""Bind native build and runtime results to source and package bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile

from prepare import VERSION, inspect_source


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def package_pattern(distribution, version):
    suffix = '-1-x86_64.pkg.tar.zst' if distribution == 'arch' else '-1.fc44.x86_64.rpm'
    return f'insidious-{version}{suffix}'


def write(path, data):
    # A failed or repeated run must not replace an earlier successful result.
    with path.open('x') as stream:
        json.dump(data, stream, indent=2)
        stream.write('\n')


def record_build(distribution, source, package, output):
    version, source_digest = inspect_source(source)
    if package.name != package_pattern(distribution, version):
        raise ValueError('Unexpected binary package filename')
    with tarfile.open(source, 'r:xz') as archive:
        metadata = json.load(archive.extractfile(f'insidious-{version}/source-metadata.json'))
    write(output, {
        'schema_version': 1,
        'distribution': distribution,
        'build_and_tests': 'passed',
        'source': {'version': version, 'commit': metadata['commit'],
                   'filename': source.name, 'sha256': source_digest},
        'package': {'filename': package.name, 'sha256': digest(package)},
    })


def verify_build(directory, distribution):
    data = json.loads((directory / 'build.json').read_text())
    source = data['source']
    package = data['package']
    if (data['schema_version'] != 1 or data['distribution'] != distribution
            or data['build_and_tests'] != 'passed'
            or not re.fullmatch(VERSION, source['version'])
            or not re.fullmatch('[0-9a-f]{40}', source['commit'])
            or source['filename'] != f"insidious-{source['version']}.tar.xz"
            or not re.fullmatch('[0-9a-f]{64}', source['sha256'])
            or package['filename'] != package_pattern(distribution, source['version'])
            or digest(directory / package['filename']) != package['sha256']):
        raise ValueError('Package build evidence or digest mismatch')
    return data


def check_previous(directory, distribution, version_file):
    data = verify_build(directory, distribution)
    match = re.fullmatch(rf'Insidious ({VERSION})\n', version_file.read_text())
    if (not match or tuple(map(int, match[1].split('.')))
            >= tuple(map(int, data['source']['version'].split('.')))):
        raise ValueError('Upgrade requires an older application version')


def record_runtime(directory, distribution, version_file, output, upgrade):
    data = verify_build(directory, distribution)
    if version_file.read_text() != f"Insidious {data['source']['version']}\n":
        raise ValueError('Installed application version mismatch')
    data['runtime_checks'] = {
        name: 'passed' for name in ('installation', 'libraries', 'desktop_metadata',
                                   'x11_startup', 'wayland_startup', 'privacy',
                                   'removal', 'settings_preservation')
    }
    data['installation_mode'] = 'upgrade' if upgrade == 'passed' else 'clean-install'
    data['upgrade'] = upgrade
    write(output, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    build = sub.add_parser('build')
    verify = sub.add_parser('verify')
    runtime = sub.add_parser('runtime')
    previous = sub.add_parser('previous')
    for command in (build, verify, runtime, previous):
        command.add_argument('distribution', choices=('arch', 'fedora'))
    build.add_argument('source', type=Path)
    build.add_argument('package', type=Path)
    build.add_argument('output', type=Path)
    for command in (verify, runtime, previous):
        command.add_argument('directory', type=Path)
    runtime.add_argument('version_file', type=Path)
    runtime.add_argument('output', type=Path)
    runtime.add_argument('upgrade', choices=('passed', 'not-run', 'not-applicable-first-package'))
    previous.add_argument('version_file', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'build':
            record_build(args.distribution, args.source, args.package, args.output)
        elif args.command == 'verify':
            print(verify_build(args.directory, args.distribution)['package']['filename'])
        elif args.command == 'previous':
            check_previous(args.directory, args.distribution, args.version_file)
        else:
            record_runtime(args.directory, args.distribution, args.version_file, args.output, args.upgrade)
    except (ValueError, OSError, KeyError, TypeError, tarfile.TarError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
