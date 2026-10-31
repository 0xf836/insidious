# Continuous Integration

CI validates pull requests and builds the Arch and Fedora targets from [packaging/platforms.json](../packaging/platforms.json). Automated native package qualification and release publication are separate stages in the [release specification](releasing.md).

## Workflows and policy

- `ci.yml` runs on pull requests (including title edits), pushes to `master`, and manual dispatch. It has no path filters: both candidates build even for documentation changes and future release PRs/commits.
- `checks.yml` exposes reusable validation and the selected matrix from `packaging/platforms.json`. It validates numeric version syntax, optional release-please manifest agreement and tag agreement when called in tag context.
- `build.yml` accepts the validated matrix as a reusable-workflow input. Each job runs the pinned official x86_64 image with a read-only source mount, copies a Git-less source export into disposable storage, installs standard distribution packages, builds Debug with tests enabled and developer audit tooling disabled, and runs the full CTest suite. Existing offscreen and HiDPI test properties are preserved. The container export identifies as `-dev+unknown`; official source-archive metadata validation remains covered by the version tests.
- `CI required` runs even after dependency failures. It accepts only successful validation and builds; failure, cancellation, missing results and unexpected skips fail the check. The matrix rejects duplicate/unknown targets, missing mandatory targets, empty candidate sets and unpinned images. The advisory title job is excluded from its dependencies and result checks.

`Release note hint` is a separate advisory job for PRs. A title without a
Conventional Commit prefix produces a notice for maintainers and succeeds;
contributors do not need to rename their PR. It does not gate builds or merging.
PR text is read from the event JSON and is never interpolated into shell commands
or echoed into workflow annotations.

Maintainers choose a Conventional Commit message when squash-merging, such as
`fix(viewer): restore zoom after rotation`, to classify future release notes.
Dependabot uses the `ci(deps)` prefix automatically. Release-please and version
bump classification remain future work.

All workflows grant only `contents: read`, do not persist checkout credentials, do not inherit secrets and do not use `pull_request_target`. Jobs run on disposable GitHub-hosted runners without release credentials. Reassess permissions and credential isolation when adding release automation.

Actions are pinned to full SHAs. Dependabot proposes monthly Action updates. Image updates are manual reviewed changes to `ci_image` and `image_digest` in the platform matrix. Container digests freeze the base image, **not** the rolling package repositories: each run upgrades from standard repositories and retains the resolved package inventory. Fedora disables weak dependencies and the optional Cisco OpenH264 repository (H.264 is outside the supported format allowlist); mandatory JPEG/PNG/WebP/BMP and archive support still runs through the regression suite. A digest is not a guarantee of bit-for-bit reproducibility.

## Artifacts and local execution

Each target uploads `build-<target>` for 14 days, including the build/package-manager log, complete installed package inventory, OS identity, repository configuration, UTC start time, CTest JUnit results, CTest logs and `ldd` output when those stages are reached. Failed early runs may have only partial evidence. Build logs contain the compiler, CMake and main dependency versions. These are developer CI artifacts, not application file logs or release packages. Keep raw build/test logs, CTest XML, full package inventories and runtime library reports in Actions artifacts or the ignored `.ci-results/` directory for local runs; do not commit them under `docs/`. Document lasting dependency requirements and compatibility constraints; keep individual run results in CI artifacts or PR discussions.

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

Use an empty output directory for each run. Docker-created output may be owned by root. Python 3 and Docker are developer/CI tools; they add no application runtime dependencies or installed contents. `BUILD_TESTING=OFF` removes test targets and the Qt Test requirement. `INSIDIOUS_BUILD_MEMORY_AUDIT=ON` explicitly enables the developer audit executable; it defaults to off and is never installed.

## Rotation regression coverage

`decoderExifRotationAndRegion` is mandatory at every thread count. Transformed rendering uses source-ordered strip evaluation, described in [Current implementation](implementation.md#sequential-decode-and-rotated-rendering). Run the rotation regressions with a low thread count using:

```sh
VIPS_CONCURRENCY=2 QT_QPA_PLATFORM=offscreen \
  build/insidious_core_tests decoderExifRotationAndRegion decoderTransformedSampling
```

## Repository policy

Protect `master` with required pull requests and the exact `CI required` check from GitHub Actions. Require the branch to be up to date, enforce protection for administrators, and disable force pushes and branch deletion. Approving reviews are optional. Enable squash merging with the PR title as the default commit title (`squash_merge_commit_title=PR_TITLE`); maintainers adjust that message before merging when release classification is needed. Do not require `Release note hint` in branch protection. Repository settings are managed separately from workflow files.

Build-only CI does not require a separate live fork PR test. Native package and release readiness remain governed by the [platform matrix](../packaging/README.md) and [release acceptance criteria](releasing.md#delivery-stages-and-acceptance).

Package qualification is an explicit [two-container check](packaging.md#package-checks)
for packaging changes and releases. It adds no jobs to ordinary PR CI. Native
recipes run the regression suite during package builds; runtime checks consume
those packages without another build or CTest pass.

GitHub semantics used here are described in [reusable workflows](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows) and [workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax).
