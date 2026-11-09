#!/usr/bin/env python3
"""Prepare locked Rust sources and notices before an offline build."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def cargo(manifest, *args):
    return subprocess.check_output(['cargo', *args, '--manifest-path', str(manifest)],
                                   cwd=manifest.parent, text=True)


def notices(manifest, output):
    packages = json.loads(cargo(manifest, 'metadata', '--frozen', '--format-version', '1'))['packages']
    output.mkdir(parents=True, exist_ok=True)
    inventory = []
    for package in sorted(packages, key=lambda p: (p['name'], p['version'])):
        if package['source'] is None:
            continue
        root = Path(package['manifest_path']).parent
        destination = output / f"{package['name']}-{package['version']}"
        destination.mkdir(exist_ok=True)
        files = sorted(p for p in root.iterdir() if p.is_file()
                       and p.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE')))
        if not files:
            # Some workspace crates omit the repository's shared license from their crate archive.
            for sibling in packages:
                if (sibling['repository'] and sibling['repository'] == package['repository']
                        and sibling['license'] == package['license']):
                    files = sorted(p for p in Path(sibling['manifest_path']).parent.iterdir()
                                   if p.is_file() and p.name.upper().startswith(('LICENSE', 'COPYING')))
                    if files:
                        break
        if not files or not package['license']:
            raise ValueError(f"Missing license for {package['name']}")
        for path in files:
            shutil.copyfile(path, destination / path.name)
        inventory.append({k: package[k] for k in ('name', 'version', 'license')})
    (output / 'THIRD_PARTY.json').write_text(json.dumps(inventory, indent=2) + '\n')
    return inventory


def vendor(manifest):
    destination = manifest.parent / 'vendor'
    if destination.exists():
        raise ValueError('Vendor destination already exists')
    config = cargo(manifest, 'vendor', '--locked', '--versioned-dirs', str(destination))
    config = config.replace(str(destination), 'vendor')
    config_dir = manifest.parent / '.cargo'
    config_dir.mkdir(exist_ok=True)
    (config_dir / 'config.toml').write_text(config)
    inventory = notices(manifest, manifest.parent / 'third-party')
    (manifest.parent / 'bundled-rust.provides').write_text(''.join(
        f"Provides: bundled(crate({p['name']})) = {p['version']}\n" for p in inventory))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('vendor', 'notices'))
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    manifest = args.manifest.resolve()
    if args.action == 'vendor':
        vendor(manifest)
    else:
        if args.output is None:
            parser.error('notices requires --output')
        notices(manifest, args.output.resolve())


if __name__ == '__main__':
    main()
