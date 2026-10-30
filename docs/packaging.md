# Native Packaging

Arch and Fedora recipes consume the same canonical `insidious-X.Y.Z.tar.xz`
source archive. Versions come from its `version.txt`; source identity follows the
[archive metadata contract](releasing.md#source-archive-metadata-contract).
Native packages use system Qt, libvips, ImageMagick loader and libarchive.
X11 startup uses Qt's `xcb` platform plugin; native Wayland startup uses its
`wayland` plugin. On the current targets, these plugins ship in
[Arch qt6-base](https://archlinux.org/packages/extra/x86_64/qt6-base/files/) and
[Fedora qt6-qtbase-gui](https://packages.fedoraproject.org/pkgs/qt6-qtbase/qt6-qtbase-gui/fedora-44.html).
A separate Qt Wayland module is not required by the viewer. Recheck plugin
packaging when qualifying a different distribution or older Qt version.
Debian/Ubuntu recipes remain deferred while those targets are excluded or blocked
in the [platform matrix](../packaging/platforms.json).

## Prepare recipes

Obtain a trusted source archive and verify its release checksums/provenance before
preparing packages. From the source checkout or extracted source tree:

```sh
python3 tools/packaging/prepare.py \
  --source downloads/insidious-0.1.0.tar.xz \
  --output .ci-results/native
```

The output directory must not exist. Preparation validates archive paths, numeric
version agreement and source metadata, then writes `PKGBUILD`, `insidious.spec`,
a copy of the source archive and `SHA256SUMS`. The Arch recipe includes the actual
SHA-256 and final public release URL. These checks establish internal consistency,
not authenticity: source metadata alone cannot prove that an archive is official.
Source creation and publication belong to the release automation stage.

## Arch

Use a clean Arch environment and run makepkg as an ordinary user:

```sh
cd .ci-results/native
sha256sum -c SHA256SUMS
SRCDEST="$PWD" makepkg -s --cleanbuild
```

Seeding `SRCDEST` allows pre-publication builds without downloading an unpublished
asset. Makepkg still verifies the checksum in the rendered recipe. The stable
`insidious` package conflicts with the root recipe's `insidious-git`; both use
shared CMake installation rules. See the [Arch guide](arch-linux.md) for the VCS
workflow.

## Fedora

Use a clean Fedora 44 environment with the spec's build requirements installed:

```sh
cd .ci-results/native
sha256sum -c SHA256SUMS
mkdir -p rpm-build/SOURCES rpm-build/SPECS
cp insidious-*.tar.xz rpm-build/SOURCES/
cp insidious.spec rpm-build/SPECS/
rpmbuild -ba --define "_topdir $PWD/rpm-build" rpm-build/SPECS/insidious.spec
```

RPM generates shared-library requirements; explicit Qt/libvips minimums and the
`vips-magick` runtime dependency retain the application requirements. `-ba`
produces binary and source RPMs. No package maintainer script fetches libraries,
changes user settings or assigns default file associations.

## Package checks

`tools/packaging/check.sh` runs **inside a disposable container only**. It installs
distribution build dependencies, builds and tests the package, installs it, checks
runtime library resolution and desktop metadata, exercises X11 and headless Wayland startup with a
space/Unicode image path, and removes the package while preserving user settings.
Run it with the pinned target image from the platform matrix:

```sh
mkdir -p .ci-results/package-arch
docker run --rm --platform linux/amd64 \
  -v "$PWD/.ci-results/native:/input:ro" \
  -v "$PWD:/src:ro" \
  -v "$PWD/.ci-results/package-arch:/results" \
  IMAGE bash /src/tools/packaging/check.sh arch
```

Use `fedora` and a separate output directory for Fedora. Input includes the prepared
recipes, source archive and `SHA256SUMS`. Logs, package inventories and built
packages remain in the explicit output directory. Python 3, native packaging
tools, desktop-file-utils, AppStream, Xvfb and Weston are developer/build tools, not new
application runtime dependencies.

Automated startup is not interactive desktop qualification. Before promoting a
native target, check menu/Open With launch on X11 and Wayland, image navigation,
rendering and archive behavior, and upgrade from the preceding package when
available. Keep `native_qualified` and release readiness false until those gates
are satisfied. See [release acceptance](releasing.md#delivery-stages-and-acceptance).
