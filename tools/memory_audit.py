#!/usr/bin/env python3

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


SCENARIOS = [
    "filesystem-fit",
    "archive-fit",
    "zoom-render",
    "prefetch-simulation",
]


def run(command, cwd=None, quiet=False):
    stderr = subprocess.DEVNULL if quiet else None
    subprocess.run(command, cwd=cwd, check=True, stderr=stderr)


def ensure_tool(name):
    path = shutil.which(name)
    if path is None:
        raise SystemExit(f"required tool not found: {name}")
    return path


def create_noise_jpeg(path, width, height):
    run([
        "vips",
        "gaussnoise",
        str(path),
        str(width),
        str(height),
        "--mean",
        "128",
        "--sigma",
        "40",
    ], quiet=True)


def create_fixtures(fixture_dir):
    fixture_dir.mkdir(parents=True, exist_ok=True)

    create_noise_jpeg(fixture_dir / "large-fs.jpg", 4800, 3200)
    create_noise_jpeg(fixture_dir / "large-archive-entry.jpg", 4800, 3200)
    for index in range(1, 4):
        create_noise_jpeg(fixture_dir / f"prefetch-{index}.jpg", 3600, 2400)

    archive_path = fixture_dir / "large-archive.zip"
    if archive_path.exists():
        archive_path.unlink()
    run([
        "zip",
        "-q",
        "-j",
        str(archive_path),
        str(fixture_dir / "large-archive-entry.jpg"),
    ])


def parse_key_value(output):
    result = {}
    for line in output.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        result[key.strip()] = value.strip()
    return result


def run_scenario(executable, fixture_dir, scenario):
    completed = subprocess.run(
        [
            str(executable),
            "--fixture-dir",
            str(fixture_dir),
            "--scenario",
            scenario,
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        sys.stderr.write(completed.stderr)
        raise subprocess.CalledProcessError(completed.returncode, completed.args, completed.stdout, completed.stderr)
    return parse_key_value(completed.stdout)


def markdown_table(rows):
    headers = [
        "Scenario",
        "Source",
        "Fixture",
        "Source MiB",
        "Archive Entry Buffer MiB",
        "Render MiB",
        "Displayed MiB",
        "Cache MiB",
        "Peak RSS MiB",
        "Notes",
    ]
    keys = [
        "scenario",
        "source",
        "fixture",
        "source_mib",
        "archive_entry_buffer_mib",
        "render_estimate_mib",
        "displayed_mib",
        "cache_mib",
        "peak_rss_mib",
        "notes",
    ]
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(row.get(key, "") for key in keys) + " |")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Run Insidious peak memory audit scenarios.")
    parser.add_argument("--build-dir", default="build", help="CMake build directory.")
    parser.add_argument("--fixture-dir", help="Directory for generated synthetic fixtures.")
    parser.add_argument("--keep-fixtures", action="store_true", help="Keep generated fixtures.")
    parser.add_argument("--scenario", choices=SCENARIOS, action="append", help="Scenario to run; defaults to all.")
    args = parser.parse_args()

    ensure_tool("vips")
    ensure_tool("zip")

    executable = Path(args.build_dir) / "insidious_memory_audit"
    if not executable.exists():
        raise SystemExit(f"audit executable not found: {executable}; run cmake --build {args.build_dir}")

    owned_temp = None
    if args.fixture_dir:
        fixture_dir = Path(args.fixture_dir)
    else:
        owned_temp = tempfile.TemporaryDirectory(prefix="insidious-memory-audit-")
        fixture_dir = Path(owned_temp.name)

    try:
        create_fixtures(fixture_dir)
        rows = [run_scenario(executable, fixture_dir, scenario) for scenario in (args.scenario or SCENARIOS)]
        print(markdown_table(rows))
        print()
        print(f"Fixture directory: {fixture_dir}")
    finally:
        if owned_temp is not None and args.keep_fixtures:
            kept = Path(tempfile.mkdtemp(prefix="insidious-memory-audit-kept-"))
            shutil.copytree(fixture_dir, kept, dirs_exist_ok=True)
            print(f"Kept fixtures: {kept}", file=sys.stderr)
        if owned_temp is not None:
            owned_temp.cleanup()


if __name__ == "__main__":
    main()
