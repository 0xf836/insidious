# Versioning, Releases, and Linux Distribution

**Status: Planned — not implemented.**

This is the canonical technical specification for future versioning, CI, release publication, and Linux packaging. It records the agreed design and acceptance gates; it does not claim that workflows, release packages, desktop integration, or platform qualification already exist. Implement it in the stages below rather than as one large change.

## Goals and current baseline

Provide a predictable maintainer workflow and complete, clearly labelled downloads: merge ordinary changes, review a generated release PR, merge it, and let CI publish the tested release. Native packages must use distribution-provided libraries. Preserve the application's privacy contract and existing rendering capabilities.

The planning baseline is:

- `CMakeLists.txt` declares version `0.1.0`, Qt >= 6.5, and vips-cpp >= 8.18.3.
- `src/app/main.cpp` exposes the CMake version through `--version` and Qt application metadata. The early version path does not require a display.
- The root `PKGBUILD` builds `insidious-git` from Git and installs through CMake.
- No workflow files or local release tags were present during planning. Remote tags and GitHub Releases must be checked before bootstrapping releases.
- Package installation currently includes the binary, documentation, and license; desktop integration is future work.

Current behavior remains documented in [Current implementation](implementation.md) and [Arch Linux package](arch-linux.md). The decisions behind this design are recorded in [ADR-020 through ADR-022](decisions.md#adr-020-release-prs-control-versioning-and-publication).

## Version policy

### One application version

Introduce root `version.txt` containing one numeric `MAJOR.MINOR.PATCH` version and a trailing newline. CMake reads and validates it before `project(VERSION ...)`; release recipes consume that value rather than maintaining independent application versions. Release-please uses its `simple` strategy to update this file and `CHANGELOG.md`.

The release-please manifest is automation state, not a second manually maintained version source. CI checks that its root package version agrees with `version.txt` in release PRs and release commits. The release tag must equal `v` followed by that version.

| Identity | Example | Meaning |
| --- | --- | --- |
| Application version | `0.2.0` | Product release identity |
| Git tag | `v0.2.0` | Immutable release commit |
| Arch package version/revision | `0.2.0-1` | Upstream version plus `pkgrel` |
| Debian package version | `0.2.0-1+deb13` | Upstream version plus target-specific packaging revision |
| Ubuntu package version | `0.2.0-1+ubuntu24.04` | Separate build for a qualified Ubuntu target |
| RPM version/release | `0.2.0`, `1%{?dist}` | Upstream version plus distribution release suffix |

These package examples describe naming, not confirmed platform support. Development VCS package versions remain distinct from stable package versions; update their extraction to read `version.txt` when that file is introduced.

Generate build metadata once for the executable and Qt application version. Use these display rules:

- Clean checkout at the matching release tag, or an official source archive: `Insidious 0.2.0`.
- Other Git checkout: `Insidious 0.2.0-dev+g<short-sha>`.
- Modified Git checkout: append `.dirty` to the development identity, including when HEAD has a release tag.
- Export without Git or official archive metadata: use `0.2.0-dev+unknown`, never imply a verified release.

Release builds require clean sources and explicit validation of version, tag, and commit. An official source archive includes generated source metadata with version and full commit SHA; that metadata must agree with `version.txt`. Do not embed local absolute paths, usernames, or build timestamps in the displayed version. Record build environment details in the release manifest instead.

Preserve headless `--version`/`-v` behavior. Add a localizable About dialog showing the same version, license, and project link. Opening About must not perform network requests. Do not change the Qt organization/application names or the settings schema merely to introduce release metadata.

### Semantic versioning and compatibility

Use SemVer with a documented application contract: command-line behavior, settings compatibility, supported opening workflows, and minimum supported platforms/dependencies. Internal C++ implementation details are not a public library ABI.

| Commit classification | Version effect |
| --- | --- |
| `fix:` | PATCH |
| `feat:` | MINOR |
| `!` or `BREAKING CHANGE:` | MINOR before 1.0; MAJOR from 1.0 onward |
| `docs:`, `test:`, `ci:`, `chore:` alone | No release by default |

Configure `bump-minor-pre-major=true` and `bump-patch-for-minor-pre-major=false`. A higher minimum OS/library requirement is a breaking compatibility change. Settings changes require explicit migration/compatibility documentation; a version bump does not itself provide a migration.

Use one ordinary release channel initially. Version `0.x` communicates pre-1.0 maturity but does not automatically make each GitHub release a prerelease. Do not introduce nightly releases, RC automation, moving version tags, or parallel maintenance branches in this stage. The transition to 1.0 is an explicit maintainer decision about the compatibility contract.

### Initial release

The intended first public version is `v0.1.0`. Before configuration, inspect remote tags and Releases and reconcile any existing publication; never overwrite an existing version. If none exists, bootstrap release-please with a one-time first-release version override and a documented history boundary. Remove the override after the initial release PR so subsequent versions are computed normally.

Keep existing Git history intact. Write a curated initial changelog describing the application as delivered, rather than treating old non-Conventional commit titles as release notes. Verify the bootstrap PR in a test repository before enabling production publication.

## Maintainer workflow and automation

Keep `master` as the integration branch. Ordinary work uses short-lived branches and squash-merged PRs. Validate PR titles as Conventional Commits and configure squash messages to use the PR title. Before merge, verify that the final squash title retains its release classification.

Release-please maintains a release PR containing the version and changelog changes. The maintainer reviews its scope and notes and merges it only after all required checks pass. Do not enable auto-merge for release PRs. This merge is the publication decision; no separate routine Publish click is required.

Use manifest-mode release-please with one root package, the `simple` release strategy, unprefixed component names, `v` tags, draft releases, and explicit tag creation for drafts. Validate the selected pinned version's draft and tag behavior before rollout. A draft must be bound to a real immutable tag so later runs can identify the release boundary.

Use a GitHub App installation token for release-please so generated PR checks run automatically. Scope the App to this repository and the required contents, pull-request, and issue/label permissions. Store its private key as a GitHub Actions secret and its application ID as configuration. Package build jobs do not receive that key. Ordinary jobs default to `contents: read`; grant publication and attestation permissions only to the jobs that need them.

Configure branch rules to require PRs and the aggregate CI check. Protect release tags against replacement/deletion, with only the release automation allowed to create them. Enable immutable releases before the first production publication. Record repository settings, App setup, and the exact required check names in the implementation runbook; do not assume these settings exist because workflow files have been merged.

### CI structure

Use reusable workflows for validation and package builds, shared between PR checks and release publication. Pin third-party Actions to full commit SHAs and use dependency-update PRs to refresh them. Pin container bases by digest, record resolved distribution package versions, and update environments through review.

Required checks:

- Ordinary code PRs: configure/build, CTest, version consistency, and PR-title validation.
- Changes affecting build files, dependencies, install rules, desktop resources, or packaging: full qualified package matrix.
- Every release PR and release commit: full qualified package matrix regardless of path filters.
- Existing offscreen and HiDPI tests retain their CTest environments.
- A stable aggregate check fails if an expected job failed, was cancelled, or was unexpectedly skipped. Documentation-only PRs may pass explicit documentation checks without pretending a build ran.

Do not expose release secrets to fork PRs or execute untrusted PR code in a privileged `pull_request_target` workflow. Build jobs upload artifacts; a separate trusted publication job verifies the expected inventory and publishes them.

Use `include(CTest)`/`BUILD_TESTING` to make test targets and Qt Test requirements optional for consumers. Keep tests enabled in CI. Put developer memory-audit tooling behind a separate build option, disabled for shipping builds and absent from installed packages.

### Publication state machine

1. Merge the approved release PR.
2. Release-please creates the matching tag and draft Release for the exact release commit.
3. Invoke the release build explicitly through dependent jobs/reusable workflows, passing tag and full SHA. Do not rely on another workflow starting from an event emitted by `GITHUB_TOKEN`.
4. Verify tag, commit, version file, release-please manifest, and changelog agreement.
5. Create the official source archive and source metadata. Transfer the archive and its SHA-256 digest through CI artifacts to package jobs. Build every package from that archive, without access to `.git`; seed the Arch source cache as described below rather than downloading an unpublished release asset.
6. Test all required packages in clean target systems.
7. Generate `release-manifest.json`, checksums, and build provenance attestations. Persist them with the payloads as CI artifacts before uploading the full artifact set to the draft.
8. Verify the remote asset inventory and digests, then publish the draft automatically and mark it Latest.

Use one serialized publication concurrency group without cancelling an in-progress publication on a new push. PR checks may cancel obsolete PR runs. A failed draft blocks another production publication until recovered or explicitly abandoned in the runbook.

## Dependency qualification and native support

### Audit before lowering requirements

The first implementation stage is an evidence-based compatibility audit. Current minimum versions remain in force until that audit justifies a change.

Repository history provides leads, not qualification results:

- Qt 6.5 was required by the first MVP (`d70d449`).
- vips-cpp 8.18.3 was added with configurable scaling quality (`059020a`); the accompanying decision cited MKS2021.
- The decoder directly uses `VIPS_KERNEL_MKS2021`; Lanczos3 alone is not sufficient evidence for the current minimum.

For each candidate distribution, record OS release, architecture, repository snapshot/date, compiler, CMake, Qt, libvips, libarchive, relevant codec packages, and test outcomes. Inspect API introduction versions, relevant upstream fixes, and distribution backports. Version-number comparison alone cannot establish either compatibility or vulnerability exposure.

Build and test with the distribution's standard repositories. Check all four scaling presets, ICC conversion and fallback, EXIF orientation, HiDPI, rotated region rendering, archive streaming, cancellation/stale work, and ownership of returned image buffers. Check supported JPEG, PNG, WebP, BMP, and DIB inputs and ZIP, 7z, and RAR sources using redistributable fixtures.

A lower minimum is acceptable only if existing capabilities and behavior remain intact. Do not remove a preset, silently substitute a kernel, or disable a feature to make an older system pass. Document any small API-equivalent compatibility adaptation and test both paths. If compatibility would require substantial rework, exclude that target from native support.

Native packages must not bundle private Qt/libvips, replace system libraries, require third-party PPAs, or download replacement libraries at installation. Do not silently compile a newer dependency in CI and claim support for an OS whose installed runtime cannot satisfy it.

### Candidate matrix

Initial architecture: x86_64 (Debian/Ubuntu package architecture name: amd64).

| Candidate | Package | Qualification status |
| --- | --- | --- |
| Arch Linux rolling | `.pkg.tar.zst` | Existing VCS recipe; release package still needs qualification |
| Debian 13 | `.deb` | Pending dependency audit and tests |
| Ubuntu 24.04 LTS | Separate `.deb` | Pending dependency audit and tests |
| Ubuntu 26.04 LTS | Separate `.deb` | Pending dependency audit and tests |
| Latest stable Fedora at implementation time | `.rpm` | Select an explicit release number, then qualify |

At planning time, the official package listings showed Qt 6.4.2 for Ubuntu 24.04 and libvips 8.16.1 for Debian 13. These conflict with the current CMake minimums and must not be presented as supported installations. Recheck repository versions during the audit.

After qualification, maintain one machine-readable platform matrix consumed by CI and release inventory validation, with explicit OS releases, architectures, image digests, and expected artifacts. Publish the same matrix in release notes. Adding/removing a qualified platform requires a reviewed change; a failed release build cannot silently remove a mandatory platform.

Debian and Ubuntu builds are separate even if they use the same recipe. Fedora support does not imply support for RHEL, Rocky Linux, AlmaLinux, or openSUSE. ARM64 is deferred.

## Package design and desktop integration

### Native recipes

Use native distribution tools as the authoritative packaging layer:

- Arch: preserve root `PKGBUILD` for `insidious-git`; add a stable recipe under `packaging/arch/`. Build in a clean Arch environment. Stable sources use the official versioned source asset and a real checksum; VCS `SKIP` semantics do not carry over.
- Debian/Ubuntu: keep debhelper packaging templates under `packaging/debian/`, stage them as `debian/` in the extracted source tree, and build with `dpkg-buildpackage` in the target environment. Generate shared-library dependencies using standard Debian helpers.
- Fedora: keep the spec under `packaging/rpm/` and build with `rpmbuild` in a clean target Fedora environment. Use native dependency generation and explicit minimums where necessary.

All recipes use CMake installation with the target distribution's directory conventions. CPack is not the primary packager: native recipes provide clearer dependency handling, package revisions, validation, and downstream maintenance for the selected families.

Stable Arch metadata and Debian changelog entries are generated from the selected release version in a staging directory. The stable Arch source checksum is computed after creating the official source archive; publish the rendered recipe separately to avoid requiring an archive to contain its own checksum. Keep generation deterministic and retain templates in Git.

The rendered stable Arch recipe retains the final public release URL. Before running `makepkg` in CI, transfer the canonical source archive from the source job's CI artifact into the clean build environment's `SRCDEST`, using the exact filename expected by the recipe. Verify its SHA-256 digest against the source job's recorded digest and run makepkg's source-integrity checks against the rendered recipe. This avoids fetching a source asset that is not public yet while testing the same archive and checksum that users will receive. After publication, separately verify that the public source URL serves those same bytes. A missing or mismatched cached source must fail the pre-publication build rather than fall back to the unpublished URL.

Ship source packages where supported (`.dsc` and associated Debian source files, Fedora `.src.rpm`) and the rendered Arch recipe alongside binaries. Preserve package-manager-native filenames; use per-target directories during CI and ensure uploaded filenames cannot collide across distributions.

Install the application, desktop resources, documentation, and licenses. Do not install tests, audit tools, build caches, or private runtime libraries. Use native desktop/cache triggers where needed; maintainer scripts must not access the network, edit user preferences, or set default file associations.

For the initial release system, an application or packaging correction after publication requires a new application PATCH release. Package revisions start at 1 for each version. Do not replace published assets with a package-only rebuild; a separate package-revision publication pipeline is deferred.

### Desktop identity

Use `io.github._0xf836.Insidious` consistently for the desktop file, icon, AppStream component, and future Flatpak application ID. The leading underscore escapes the GitHub owner's initial digit to satisfy D-Bus well-known name rules, which Flatpak application IDs follow. This does not rename the existing QSettings namespace.

Provide a scalable icon, appropriate raster sizes, a localizable desktop entry, and AppStream metadata. Use `Exec=insidious %f` for the current one-path CLI, `Terminal=false`, and suitable Graphics/Viewer categories. Do not use `%F` until the command-line interface explicitly supports multiple paths.

Register Open With MIME types only for the supported image/archive allowlists. Do not advertise every format exposed by the system libvips build or make Insidious the default archive handler. Include project/help links and the MIT license in metadata; keep application metadata consistent with release versions.

Validate the application ID against Flatpak/D-Bus naming rules, including rejection of components starting with digits, and check identity agreement across desktop, icon, and AppStream resources. Validate with desktop-file and AppStream validators. Exercise launching paths containing spaces and Unicode, plus normal menu launch, on X11 and Wayland.

Desktop integration must not add application-owned recent-item writes. A separate file manager or desktop shell may record its own activity; do not claim that a desktop entry can prevent all traces created by other applications. Retain the existing non-native file-dialog policy unless a separate privacy decision changes it.

## Release contents, integrity, and presentation

Use the title `Insidious X.Y.Z`. Release notes contain a short highlights section, features/fixes, compatibility changes, known limitations, a platform/download table, installation and verification instructions, and a full changelog link. Edit the generated release PR notes for clarity before merge.

Required published artifacts:

- `insidious-X.Y.Z.tar.xz`: canonical source archive with version/source metadata, license, documentation, and recipe templates.
- Binary packages for every qualified target, plus source packages/rendered recipes as described above.
- `release-manifest.json`: version, tag, full source SHA, target identities, build environment/dependency versions, and each payload's filename, size, and SHA-256 digest.
- `SHA256SUMS`: payload and release-manifest digests, excluding the checksum file itself.
- Downloadable provenance bundles for the payloads and manifest, plus GitHub artifact attestations.

Validate package and source filenames, not just the number of uploaded assets. Do not treat GitHub's automatically generated source downloads as the canonical package source; use the explicitly generated and checksummed asset.

Set `SOURCE_DATE_EPOCH` from the release commit and normalize source archive ordering and timestamps. Record resolved dependencies and tools. These steps improve repeatability but are not a claim of bit-for-bit reproducible binaries until independently tested.

Attestations establish build provenance; SHA-256 detects corruption but does not authenticate the publisher by itself. Provide verification instructions using `sha256sum` and `gh attestation verify` with this repository as the expected identity. Checksums and provenance do not substitute for future apt/RPM repository signing. Own package repositories and their signing-key lifecycle are deferred.

## Release runbook and recovery contract

### One-time setup

1. Verify remote release history, default branch, repository visibility, and availability of attestations/immutable releases.
2. Finish dependency qualification and commit the explicit platform matrix.
3. Configure the GitHub App, scoped secrets, workflow permissions, branch/tag rules, and immutable releases.
4. Validate bootstrap, draft/tag handling, PR checks, recovery, and publication in a test repository. Use a valid test version and change only the test repository's state.
5. Enable production automation only after the complete trial passes. Record configuration ownership and credential rotation instructions.

### Routine release

1. Review the release PR's version, compatibility notes, changelog, and intended platform set.
2. Verify the full required checks and package smoke-test results. Record X11/Wayland manual acceptance on the release PR before merge.
3. Merge the release PR; monitor the explicit publication workflow through its final verification.
4. Confirm the published tag resolves to the intended commit, all downloads are present, digests/attestations verify, and Latest points to the completed release.

A green ordinary development PR is not release evidence. A successful package build is not proof of successful publication.

### Failure and retry

- Build/test failure: keep the draft unpublished and retain logs/artifacts. Retry transient infrastructure failures against the same SHA; code fixes require a new version.
- Interrupted upload: resume the draft after verifying its tag/SHA and the persisted `release-manifest.json`. Reuse existing assets only if their digests match. A same-name mismatch is a hard failure, never an automatic overwrite.
- Publication failure after upload: verify the complete remote inventory and retry publication. If publication already succeeded, verify it and exit successfully without changing assets.
- Incomplete/abandoned draft: explicitly record abandonment, retain any existing tag, and use a new version for changed sources. Never move a previously created release tag.
- Fault discovered after publication: publish a corrective version. If necessary, point Latest back to the previous verified release while the fix is prepared. Record the problem in the corrective release/issue; do not replace immutable assets.

Provide a trusted `workflow_dispatch` recovery entry point accepting an existing release tag. Always validate the tag's syntax, ancestry on the release branch, full SHA, version, originating release PR, release-please manifest, and draft/published state before acting. Distinguish the release-please manifest (version automation state in the tagged source) from `release-manifest.json` (artifact inventory generated after package validation). Recovery then follows these states:

- **Draft before release manifest generation:** validate the platform matrix from the tagged commit and repeat the source/package build and all mandatory checks at that SHA. No `release-manifest.json` is required to restart this stage. Reuse retained source artifacts only after checking their source identity and recorded digest. Unexpected remote payload assets at this stage require investigation before proceeding.
- **Draft after release manifest generation or partial upload:** retrieve the persisted `release-manifest.json` and payloads from the original trusted CI run, verify their source identity, target matrix, and digests, and compare every existing remote asset against that inventory. Resume only missing uploads and publication checks. If the trusted inventory or matching payloads cannot be recovered, stop for explicit abandonment/new-version handling rather than inventing an inventory from remote files or overwriting them.
- **Published release:** verify the complete published inventory and provenance, then exit without changing assets. Missing or mismatched files are an error requiring a corrective release.

The recovery entry point cannot create arbitrary releases from user-supplied refs or bypass qualification checks.

## Deferred distribution channels

GitHub Releases is the first download channel. AUR publication, hosted apt/RPM repositories, ARM64, nightly/RC channels, and an in-app updater are separate future work. Native recipes and generated metadata should remain suitable for downstream packaging without promising acceptance into distribution repositories.

Flatpak is the planned alternative for hosts excluded by native dependency qualification. Its design must use the same application ID, select a supported Qt-compatible runtime, and explicitly describe any dependencies built inside the Flatpak environment. The prohibition on private libraries applies to native packages; Flatpak's runtime/module model is a distinct future design, not a hidden native-package workaround.

Before implementing Flatpak, resolve file access, adjacent-folder navigation, archives, clipboard behavior, X11/Wayland, and portal/recent-item privacy. Do not assume opening one file through a portal automatically grants access to its neighbors. Flathub publication and permission choices require that later specification; this document does not claim Flatpak support is implemented.

## Delivery stages and acceptance

| Stage | Deliverable | Exit criteria |
| --- | --- | --- |
| 1. Compatibility | Dependency audit and qualified platform matrix | API/fix evidence, builds and behavior tests; explicit excluded targets and reasons |
| 2. Version identity | `version.txt`, generated metadata, About, VCS recipe adaptation | Consistent versions; headless output; clean/dirty/unknown/archive cases tested |
| 3. CI | Reusable build/test jobs and PR conventions | Required aggregate check handles failures and skips; untrusted PRs have no release credentials |
| 4. Packages and desktop | Native recipes, install rules, icon/desktop/AppStream | Package checks, installation/upgrade/removal, X11/Wayland launch acceptance |
| 5. Release automation | Release-please and verified draft-to-public pipeline | Test-repository release, retry, conflict, and missing-artifact scenarios pass |
| 6. First release | Maintainer-reviewed version and full published artifact set | Runbook completed; remote downloads and provenance independently verified |
| 7. Flatpak | Separate specification and implementation | Runtime, permissions, navigation, and privacy qualified before publication |

For package acceptance, test fresh installation, upgrade from the preceding package when available, and removal in clean environments. Check runtime library resolution, install paths, desktop validation, settings preservation, and absence of application-created history/cache/log files. Package-manager logs are distinct from application logs.

Rendering acceptance must cover the supported formats, all scaling presets, ICC/EXIF, rotated region rendering, and HiDPI. Document pre-existing bugs separately; do not claim packaging fixes archive or zoom bugs tracked elsewhere in the roadmap.

Release tests must cover a missing mandatory target, tag/version mismatch, early build failure before `release-manifest.json` exists, duplicate asset with a different digest, interrupted upload with the release manifest retained only in CI artifacts, unavailable recovery artifacts, rerun after publication, source archive without Git, and fork PR isolation. Arch acceptance must include building from the seeded source cache while the public source URL is unavailable, rejecting a mismatched source digest, and checking the public source URL after publication. Publication is allowed only after all mandatory target results and release-manifest checks pass.

The documentation stage itself is complete when this specification, ADRs, roadmap, documentation index, README, Arch guide, and agent guidance agree; local links resolve; and `git diff --check` passes. No application build or hosted release proof is implied by documentation validation.

## References

These are design references, not promises that an upstream service or distribution version will remain unchanged. Recheck tool schemas and platform package versions during implementation.

- [Semantic Versioning 2.0.0](https://semver.org/)
- [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/)
- [Release Please Action](https://github.com/googleapis/release-please-action)
- [Release Please strategies](https://github.com/googleapis/release-please/blob/main/docs/customizing.md)
- [Release Please manifest configuration](https://github.com/googleapis/release-please/blob/main/docs/manifest-releaser.md)
- [GitHub workflow triggering and token behavior](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow)
- [GitHub release management and draft publication](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)
- [GitHub artifact attestations](https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations)
- [Ubuntu 24.04 Qt package listing](https://packages.ubuntu.com/search?keywords=qt6&suite=noble)
- [Debian libvips package listing](https://packages.debian.org/search?keywords=libvips42)
- [Fedora libvips package listing](https://packages.fedoraproject.org/pkgs/vips/vips/)
- [Desktop Entry Specification](https://specifications.freedesktop.org/desktop-entry/latest/)
- [Flatpak application ID conventions](https://docs.flatpak.org/en/latest/conventions.html#application-ids)
- [D-Bus bus-name rules and escaping leading digits](https://dbus.freedesktop.org/doc/dbus-specification.html#message-protocol-names-bus)
- [Flatpak runtime, dependencies, and sandbox concepts](https://docs.flatpak.org/en/latest/basic-concepts.html)
