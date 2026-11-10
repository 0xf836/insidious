# Changelog

## [0.4.0](https://github.com/0xf836/insidious/compare/v0.3.0...v0.4.0) (2026-11-10)


### Features

* **settings:** add default comparison mode ([6ebe52b](https://github.com/0xf836/insidious/commit/6ebe52b47fe85a9ba9872ac52ecea2fc4b827a6e))
* **settings:** add default comparison mode ([9064e6c](https://github.com/0xf836/insidious/commit/9064e6c4c6b4be4256ced983b77a160ab72aba0e))

## [0.3.0](https://github.com/0xf836/insidious/compare/v0.2.0...v0.3.0) (2026-11-09)


### Features

* **decoder:** add experimental jxl-rs JPEG XL decoding ([79d1b6d](https://github.com/0xf836/insidious/commit/79d1b6dc40827a2737df01150770d4b7b49d7b68))
* **decoder:** complete jxl-rs JPEG XL integration ([03bc0aa](https://github.com/0xf836/insidious/commit/03bc0aa0bc3820045d2c7c41fadb1d5033e90648))


### Bug Fixes

* distinguish unsupported archive encryption ([#17](https://github.com/0xf836/insidious/issues/17)) ([8acfd5a](https://github.com/0xf836/insidious/commit/8acfd5a5e5ab09effecce50ad11ba5e8260a52b7))


### Performance

* **decoder:** parallelize experimental jxl-rs decoding ([35d0658](https://github.com/0xf836/insidious/commit/35d06587df821967381323ba5b24a12015acb06a))


### Build and Packaging

* **deps:** bump brotli-decompressor from 6.0.0 to 6.3.0 in /rust/jxl_bridge ([1949f9d](https://github.com/0xf836/insidious/commit/1949f9d761b3864494cf9f46a5081b7c41a00fa1))
* **deps:** bump brotli-decompressor in /rust/jxl_bridge ([eba456c](https://github.com/0xf836/insidious/commit/eba456cc30f211c0c499841950beb903a0447090))

## [0.2.0](https://github.com/0xf836/insidious/compare/v0.1.0...v0.2.0) (2026-11-02)


### Features

* **formats:** add AVIF and JPEG XL support ([b4a1144](https://github.com/0xf836/insidious/commit/b4a1144985907affefe395ea7cfa15b31a86ad1f))
* **formats:** add first-frame GIF support ([091140d](https://github.com/0xf836/insidious/commit/091140d88e5c3f382c5fe2ac2da6e87c27e3b9ba))
* **formats:** add first-page TIFF support ([54a3996](https://github.com/0xf836/insidious/commit/54a39964bb8916bf599c6b6177c4354380c43aeb))
* **formats:** add JPEG 2000 and HEIC support ([b9ad139](https://github.com/0xf836/insidious/commit/b9ad1395c56c7727c67a89b71978212fc01f8879))
* **viewer:** add animated GIF, WebP and JPEG XL playback ([50c3576](https://github.com/0xf836/insidious/commit/50c3576308593e61fbd72adf854acd216d2f6723))

## [0.1.0](https://github.com/0xf836/insidious/releases/tag/v0.1.0) (2026-11-01)

Initial public release of Insidious, a native Linux image viewer.

### Image viewing

- JPEG, PNG, WebP, BMP and DIB images, with lazy folder navigation, temporary
  file lists, and streaming ZIP, 7z and RAR access.
- Configurable scaling, embedded ICC conversion to sRGB, EXIF orientation,
  view-only rotation and physical-pixel actual size on HiDPI screens.
- Fullscreen overlays, configurable viewer shortcuts, metadata, lazy thumbnails
  and temporary image comparison.

### Privacy

- Memory-only image and thumbnail caches, archive access and comparison sessions.
- No application-owned activity history, persistent thumbnails, archive
  extraction, stored archive passwords or normal file logs.

### Downloads and compatibility

- Native packages for Arch Linux x86_64 and Fedora 44 x86_64, using system
  libraries, with desktop and AppStream integration.
- Source archive, rendered package recipes, Fedora source RPM and SHA-256
  checksums alongside the binary packages.
- Qt 6.6+ and libvips 8.18.3+ are required. Debian/Ubuntu native packages,
  Flatpak and ARM64 builds are not included in this release.

### Known limitations

- Password-protected archive opening, high zoom levels and some BMP/DIB archive
  inputs have known issues tracked in the [roadmap](docs/roadmap.md).
- Display-profile matching, animated playback and RAW development are outside
  the current product scope.

See the [user guide](docs/user-guide.md) for controls and the
[platform matrix](packaging/README.md) for native dependency exclusions.
