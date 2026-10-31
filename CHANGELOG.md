# Changelog

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
