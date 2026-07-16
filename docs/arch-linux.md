# Arch Linux Package

The repository provides a VCS package recipe for current development builds. It produces `insidious-git`, which tracks the `master` branch of the public repository and installs the native binary, full documentation tree, and MIT license through CMake.

## Build and install

Install the required Arch build and runtime dependencies:

```sh
sudo pacman -S --needed base-devel cmake git ninja pkgconf qt6-base libvips libarchive zip
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
- documentation under `/usr/share/doc/insidious`;
- the `PKGBUILD` source recipe alongside the installed documentation;
- the MIT license under `/usr/share/licenses/insidious`.

## Versioning and release packages

`insidious-git` derives its package version from the CMake project version, the current commit count, and the short Git revision. Its VCS source intentionally uses `b2sums=('SKIP')`, which is the accepted makepkg policy for a moving Git source.

When the project publishes a tagged release, add a separate non-VCS `insidious` recipe that downloads the immutable release archive and records its real checksum. Do not reuse the VCS recipe as a release package or publish a release archive recipe with `SKIP` checksums.
