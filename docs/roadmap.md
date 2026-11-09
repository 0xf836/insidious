# Roadmap

This roadmap is a living statement of direction, not a release schedule or compatibility promise. Current capabilities are documented in the [README](../README.md), [user guide](user-guide.md), and [project evolution](project-evolution.md).

## Next

### Encrypted 7z support

- Track upstream [libarchive PR #3388](https://github.com/libarchive/libarchive/pull/3388), which proposes read-side AES-256 support for 7z contents and encrypted headers. It is not yet merged; builds without the required crypto support retain the unsupported-encryption behavior.
- Qualify encrypted 7z only after the implementation is merged, released, and available in target-distribution libraries. Verify correct and incorrect passwords, cancellation, header enumeration, sequential display reads, and bounded original-byte reads. Keep passwords in memory and avoid disk extraction.
- Encrypted RAR support is deferred. No alternate archive backend is planned for it; unsupported encryption must remain a terminal error rather than an incorrect-password retry.

### Quick Jump polish

- Make the fullscreen search surface more compact.
- Route `Up` and `Down` to the result list while the search field has focus.

### Fullscreen restoration under tiling window managers

- Reproduce and isolate the lower-edge artifact observed after leaving fullscreen with the dark theme under i3wm.
- Ensure fullscreen entry and exit restore the exact normal layout without stale overlay or backing-store artifacts.

### Archive performance characterization

- Reproduce tiny BMP archive decode failure and headerless DIB streaming limitations; current file and archive loaders differ.
- Benchmark navigation through large ZIP, 7z, and RAR sources.
- Measure repeated header traversal and format-specific access costs.
- Revisit speculative archive prefetch only after the access pattern is understood.

## Later

### Rendering and interaction

- Research display-profile and monitor-profile matching as a separate color-management layer.
- Consider a temporary magnifier mode that follows the cursor while the left button is held and restores the previous view on release.

### Comparison

- Evaluate additional comparison modes only when they solve a concrete workflow not covered by Row, Grid, Carousel, Hover Switch, and alignment presets.

### Formats

- Inventory additional candidates and verify loader availability, target-distribution codec dependencies, rendering correctness, and memory behavior before enabling each format.
- Define page selection behavior before enabling additional multi-page formats.
- Treat RAW as a separate research effort because it requires preview policy, color processing, and heavier dependencies.

### Animation playback

- Investigate APNG and timed AVIF/HEIF sequences separately; evaluate codec APIs and system dependencies before extending the shared player. Multiple still items in a container must not become animation automatically.
- Keep thumbnails and comparison views on a representative still frame unless animated presentation is explicitly added to those workflows.

### Input customization

- Extend shortcut editing beyond core viewer commands if users need it.
- Add shortcut profiles, import/export, or scopes only after concrete demand establishes their semantics.

### Distribution

Follow the [release specification](releasing.md) for distribution design and acceptance criteria. Native Arch/Fedora packages are available through [GitHub Releases](https://github.com/0xf836/insidious/releases). Version identity, [CI](ci.md), desktop integration, [package qualification](packaging.md) and production release automation are implemented.

- Design Flatpak for systems excluded from native support. Keep AUR publication, hosted apt/RPM repositories, and ARM64 as separate future work.

Revisit native Debian/Ubuntu recipes when dependency exclusions are resolved.

### Memory auditing

- Add thumbnail generation and cache scenarios before increasing thumbnail cache size or preload range.

## Explicit non-goals

- Persistent recent-file, recent-folder, or recent-archive history.
- Persistent thumbnail or decoded-image caches by default.
- Persistent comparison sessions or selected comparison paths.
- Archive extraction to disk during normal viewing.
- Archive password storage.
- File logging during normal use.
- Silent support for every format exposed by the system image stack.

JPEG XL progressive display, HDR tone mapping and native-source raster reuse remain
separate investigations. The current jxl-rs backend and its limits are described
in [the requirements](requirements.md), rather than treated as future format support.
