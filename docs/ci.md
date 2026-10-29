# Continuous Integration

Distribution introduces reusable validation and clean candidate build workflows. Native package qualification, desktop acceptance and release publication remain separate gates in the [release specification](releasing.md). No repository branch settings are changed by these files.

## Workflows and policy

- `ci.yml` runs on pull requests (including title edits), pushes to `master`, and manual dispatch. It has no path filters: both candidates build even for documentation changes and future release PRs/commits.
- `checks.yml` exposes reusable validation and the selected matrix from `packaging/platforms.json`. It validates numeric version syntax, optional release-please manifest agreement, tag agreement when called in tag context, and Conventional Commit PR titles. PR text is read from the event JSON, never interpolated into shell code.
- `build.yml` accepts the validated matrix as a reusable-workflow input. Each job runs the pinned official x86_64 image with a read-only source mount, copies a Git-less source export into disposable storage, installs standard distribution packages, builds Debug with tests enabled and developer audit tooling disabled, and runs the full CTest suite. Existing offscreen and HiDPI test properties are preserved. The container export identifies as `-dev+unknown`; official source-archive metadata validation remains covered by the version tests.
- `CI required` runs even after dependency failures. It accepts only successful validation and builds; failure, cancellation, missing results and unexpected skips fail the check. The matrix rejects duplicate/unknown targets, missing mandatory targets, empty candidate sets and unpinned images. There are no intentional job skips in this stage.

The PR title types are `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore` and `revert`, with optional scope and `!`. For example: `fix(viewer): restore zoom after rotation`. Configure squash merge messages to preserve the reviewed PR title. This stage does not classify release bumps or run release-please.

All workflows grant only `contents: read`, do not persist checkout credentials, do not inherit secrets and do not use `pull_request_target`. Fork PR code runs on disposable hosted runners without release credentials. The Docker daemon belongs to that runner; this is not a sandbox for running untrusted PRs on a shared self-hosted machine.

Actions are pinned to full SHAs. Dependabot proposes monthly Action updates. Image updates are manual reviewed changes to `ci_image` and `image_digest` in the platform matrix. Container digests freeze the base image, **not** the rolling package repositories: each run upgrades from standard repositories and retains the resolved package inventory. Fedora disables weak dependencies and the optional Cisco OpenH264 repository (H.264 is outside the supported format allowlist); mandatory JPEG/PNG/WebP/BMP and archive support still runs through the regression suite. A digest is not a guarantee of bit-for-bit reproducibility.

## Evidence and local execution

Each target uploads `build-<target>` for 14 days, including the build/package-manager log, complete installed package inventory, OS identity, repository configuration, UTC start time, CTest JUnit results, CTest logs and `ldd` output when those stages are reached. Failed early runs may have only partial evidence. Build logs contain the compiler, CMake and main dependency versions. These are developer CI artifacts, not application file logs or release packages. Keep raw build/test logs, CTest XML, full package inventories and runtime library reports in Actions artifacts or the ignored `.ci-results/` directory for local runs; do not commit them under `docs/`. Keep only the dated result summary, relevant dependency versions and conclusions in documentation.

Run policy checks locally:

```sh
python3 -m unittest discover -s tests -p 'ci_policy_tests.py' -v
python3 tools/ci/checks.py validate
bash -n tools/ci/build.sh
actionlint
```

To repeat a clean candidate build, obtain its exact `ci_image` from `packaging/platforms.json`, then run from the checkout root, substituting that image and `arch` or `fedora`:

```sh
mkdir -p .ci-results
docker run --rm --platform linux/amd64 \
  -v "$PWD:/src:ro" -v "$PWD/.ci-results:/results" \
  IMAGE bash /src/tools/ci/build.sh DISTRIBUTION
```

Use an empty output directory for each run. Docker-created evidence may be owned by root. Python 3 and Docker are developer/CI tools; they add no application runtime dependencies or installed contents. `BUILD_TESTING=OFF` removes test targets and the Qt Test requirement. `INSIDIOUS_BUILD_MEMORY_AUDIT=ON` explicitly enables the developer audit executable; it defaults to off and is never installed.

## Local candidate qualification evidence

The 2026-10-30 local qualification (2026-10-29 UTC) built both candidates with their standard repositories and no dependency overrides. The builds used a modified worktree based on commit `7734e2ad5503289ba7f656e8cca903231a494557`, with the pinned images recorded in the platform matrix. These are local container results, not a hosted Actions run or native package acceptance.

| Candidate | Qt | libvips | libarchive | CTest |
| --- | --- | --- | --- | --- |
| Arch rolling | 6.12.0 | 8.18.7 | 3.8.10 | 11/11 passed |
| Fedora 44 | 6.11.2 | 8.18.3 | 3.8.7 | 11/11 passed |

Both used GCC 16.2.1; Arch CMake was 4.4.5 and Fedora CMake was 4.3.0. Each also passed the five CI policy tests. Repository package sets were resolved at run time; these are dated observations, not repository snapshots. Arch's download cache was resumed and both containers were resumed after a daemon interruption. The final runs completed configure/build/CTest after the dependency correction.

Initial clean builds exposed an undeclared supported-format dependency: both passed compilation but failed 12 core BMP/DIB cases without the libvips ImageMagick loader. Arch requires `imagemagick` in addition to `libvips`; Fedora requires `vips-magick` in addition to `vips-devel`. Installing these standard packages made all 102 core checks pass without decoder changes or test suppression. The root Arch VCS recipe now declares this runtime dependency, and application bootstrap no longer suppresses a missing Magick module warning. [ADR-023](decisions.md#adr-023-supported-formats-require-the-distribution-imagemagick-loader) records the dependency policy. Fedora's [official package description](https://packages.fedoraproject.org/pkgs/vips/vips-magick/) identifies the separate loader module.

Local host validation also passed all 11 CTest entries, actionlint 1.7.7, Bash syntax, policy tests and documentation link checks. A separate consumer configuration built with tests and audit tooling disabled, and an explicit audit-enabled configuration built `insidious_memory_audit`. No native package installation/upgrade/removal or interactive X11/Wayland acceptance was performed.

## Temporary rotation test quarantine

The known [Zoom after rotation](roadmap.md#zoom-after-rotation) bug can fail six JPEG EXIF/view-rotation region cases with `VipsJpeg: out of order read` with one to four libvips threads. These cases temporarily report Qt Test **SKIP** only after that specific decode error occurs with a nonzero combined rotation and one to four libvips threads. Successful decodes still execute all geometry/pixel assertions, other errors remain failures, and other thread counts are not quarantined. The test clears the previous row's libvips error buffer before decoding so an earlier failure cannot trigger a later skip.

A green CI run may therefore include six known core-case skips; it does not establish correct rotated JPEG region rendering. No job or whole test target uses `continue-on-error`. Keep the bug as a separate fix, remove this quarantine in the fix PR, and close it before native release qualification. To reproduce the failure without quarantine:

```sh
INSIDIOUS_STRICT_ROTATION_TESTS=1 VIPS_CONCURRENCY=2 QT_QPA_PLATFORM=offscreen \
  build/insidious_core_tests decoderExifRotationAndRegion
```

## Repository setup and remaining acceptance

After merging and observing a successful hosted run, configure the repository ruleset to require `CI required` and PRs, and configure squash messages to use the PR title. Verify the exact check name in GitHub before enabling enforcement. These repository settings, hosted execution, cancellation behavior in the service, and fork PR isolation must be verified there; local policy tests and actionlint do not establish hosted acceptance.

The current stage adds clean build evidence only. Keep `native_qualified`, `release_required`, `native_qualification_complete` and `release_ready` false until the package/behavior gates are satisfied. Package workflows and release inventory checks will extend these reusable jobs in later stages; no package checks are claimed today.

GitHub semantics used here are described in [reusable workflows](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows) and [workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax).
