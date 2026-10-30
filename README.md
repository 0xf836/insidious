# Insidious

Insidious is a fast, privacy-conscious image viewer for Linux. It opens individual images, folders, selected file sets, and image archives without turning viewing into a cataloguing workflow.

The application is built around predictable navigation, high-quality rendering, responsive background work, and a strict no-history policy.

## Highlights

- Browse images from files, folders, ZIP/7z/RAR archives, and homogeneous multi-source selections.
- Navigate lazy folder and archive trees without blocking the interface.
- View images with high-quality libvips scaling, HiDPI-aware 1:1 rendering, embedded ICC conversion to sRGB, view-only rotation, and configurable region rendering.
- Use list and lazy thumbnail views, quick filename jump, natural sorting, and configurable keyboard or mouse shortcuts.
- Compare images in Row, Grid, or Carousel layouts, or switch between two images on hover.
- Enter a clean fullscreen view with edge-triggered controls that do not resize or reload the image.
- Inspect basic file information, color profile state, and selected EXIF fields.
- Copy the original image bytes instead of the transformed display raster.

## Privacy by design

Insidious does not keep a history of opened files, folders, or archives. It does not create persistent thumbnail or decoded-image caches, extract archives to disk during normal use, save archive passwords, persist comparison sessions, or write file logs.

Application settings are stored through Qt and contain interface preferences only. They do not contain viewed paths or image data. See [Privacy and traces](docs/requirements.md#privacy-and-traces) for the complete contract.

## Supported formats

| Kind | Formats |
| --- | --- |
| Images | JPEG, PNG, WebP, BMP, DIB |
| Archives | ZIP, 7z, RAR |

Support is intentionally based on an explicit allowlist. Availability of a codec in the system libvips installation does not automatically enable an additional format in Insidious.

## Build from source

Requirements:

- Linux
- CMake 3.24 or newer
- Ninja
- C++20 compiler
- Qt 6.6 or newer with Core, Gui, Widgets, Concurrent, and Test components
- libvips/vips-cpp 8.18.3 or newer, including its system ImageMagick loader for BMP/DIB
- libarchive
- pkg-config

Arch Linux and similar systems:

```sh
sudo pacman -S qt6-base libvips imagemagick libarchive cmake ninja pkgconf git bash
```

Debian, Ubuntu, and similar systems:

Check the [dependency audit](docs/dependency-audit.md) and [platform matrix](packaging/platforms.json) before using the package names below. Arch and Fedora 44 are candidates; clean native qualification is deferred. Standard Ubuntu 24.04 lacks the required Qt API, and Ubuntu 26.04 currently ships libvips 8.18.0, below the retained 8.18.3 minimum. These commands do not imply native support for those releases.

```sh
sudo apt install qt6-base-dev libvips-dev libarchive-dev cmake ninja-build pkg-config git bash
```

Configure, build, and test:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the viewer:

```sh
./build/insidious
./build/insidious /path/to/image-folder-or-archive
```

For an Arch Linux package, use the included [`PKGBUILD`](PKGBUILD):

```sh
makepkg -s
sudo pacman -U insidious-git-*.pkg.tar.zst
```

See [Arch Linux package](docs/arch-linux.md) for the VCS recipe and [native packaging](docs/packaging.md) for stable Arch/Fedora recipe preparation and desktop integration. Native platform qualification remains pending.

## Essential controls

| Action | Default input |
| --- | --- |
| Open file / folder | `Ctrl+O` / `Ctrl+Shift+O` |
| Previous / next image | `Left` or `PageUp` / `Right` or `PageDown` |
| Previous / next folder | `MouseBack` / `MouseForward` |
| Quick filename jump | `Ctrl+F` |
| Zoom | `Ctrl+wheel`, standard zoom shortcuts, or toolbar controls |
| Fit / actual size | `0` / `1` |
| Rotate left / right | `[` / `]` |
| Copy original image | `Ctrl+C` |
| Toggle fullscreen | `F11` or left double click |
| Leave fullscreen | `Esc` |

Wheel navigation, zoom behavior, rendering quality, fullscreen presentation, theme, and core shortcuts can be changed in Preferences. The [user guide](docs/user-guide.md) covers source browsing, comparison modes, mouse gestures, and all default controls.

## Documentation

- [Documentation index](docs/README.md) — where to start for each audience.
- [User guide](docs/user-guide.md) — opening, browsing, viewing, comparing, and configuring images.
- [Product principles](docs/product.md) — goals, values, and deliberate non-goals.
- [Architecture](docs/architecture.md) — system boundaries and major data flows.
- [Current implementation](docs/implementation.md) — code map for maintainers and coding agents.
- [Arch Linux package](docs/arch-linux.md) — build and install the VCS package with makepkg.
- [Versioning and distribution](docs/releasing.md) — current build identity and planned CI, releases, native packages, and platform qualification.
- [Roadmap](docs/roadmap.md) — planned work and research directions.
- [Project evolution](docs/project-evolution.md) — how the initial viewer foundation grew into the current application.

Repository-specific instructions for coding agents are in [AGENTS.md](AGENTS.md). Complex changes should also follow [PLANS.md](PLANS.md).

## License

Insidious is available under the [MIT License](LICENSE).
