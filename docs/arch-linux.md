# Arch Linux Package

The repository provides a VCS package recipe for current development builds. It produces `insidious-git`, which tracks the `master` branch of the public repository and installs the native binary, desktop entry, icons, AppStream metadata, full documentation tree, and MIT license through CMake.

The recipe declares Qt >= 6.6 and libvips >= 8.18.3, matching CMake. It also requires `imagemagick`: libvips uses that system loader for the supported BMP/DIB inputs; the optional dependency in the libvips package is mandatory for this viewer. The [dependency audit](dependency-audit.md) explains these minimums; an older cached package set is not sufficient merely because the package names match.

## Build and install

Install the required Arch build and runtime dependencies:

```sh
sudo pacman -S --needed base-devel cmake git ninja pkgconf qt6-base libvips imagemagick libarchive zip python desktop-file-utils appstream
```

Clone the repository and build the package from its root:

```sh
makepkg -s
```

Install the resulting package:

```sh
sudo pacman -U insidious-git-*.pkg.tar.zst
```

Use `makepkg -si` to build and install in one step. The package runs the Qt test suite during `check()`; use normal makepkg options only when an explicit local packaging policy requires skipping checks.

## Package contents

The package installs:

- `/usr/bin/insidious`;
- the desktop entry, AppStream metadata and hicolor icons under `/usr/share`;
- documentation under `/usr/share/doc/insidious`;
- the `PKGBUILD` source recipe alongside the installed documentation;
- the MIT license under `/usr/share/licenses/insidious`.

## Versioning and release packages

`insidious-git` derives its package version from `version.txt`, the current commit count, and the short Git revision. CMake uses the same numeric source version; the application displays a generated release/development identity as described in the [version policy](releasing.md#one-application-version). Its VCS source intentionally uses `b2sums=('SKIP')`, which is the accepted makepkg policy for a moving Git source.

Stable `insidious` recipes are prepared separately from a checksummed canonical
source archive; see [native packaging](packaging.md). The stable recipe never uses
`SKIP`. Both variants install through the same CMake rules. AUR publication and
release publication are separate stages.
