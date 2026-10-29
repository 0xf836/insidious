# Roadmap

This roadmap is a living statement of direction, not a release schedule or compatibility promise. Current capabilities are documented in the [README](../README.md), [user guide](user-guide.md), and [project evolution](project-evolution.md).

## Next

### Password-protected archive opening

- Reproduce and fix the reported failure when opening a password-protected archive.
- Verify successful opening with a correct password, retry after an incorrect password, and cancellation without losing navigation state. Keep passwords in memory only and do not extract entries to disk.

### High zoom levels

- Reproduce and fix the reported inability to reach high zoom levels.
- Check zoom limits, region rendering, and render-memory budget handling so supported zoom levels remain usable without bypassing memory safeguards.

### Zoom after rotation

- Reproduce and fix the reported error when zooming after rotating an image.
- Verify zoom after left and right rotation, including fit, actual-size, and region-rendered views.

### Quick Jump polish

- Make the fullscreen search surface more compact.
- Route `Up` and `Down` to the result list while the search field has focus.

### Fullscreen restoration under tiling window managers

- Reproduce and isolate the lower-edge artifact observed after leaving fullscreen with the dark theme under i3wm.
- Ensure fullscreen entry and exit restore the exact normal layout without stale overlay or backing-store artifacts.

### Archive performance characterization

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

- Expand the supported format allowlist to include AVIF, JPEG 2000, JPEG XL, HEIC, GIF, and other libvips-supported formats that fit the existing viewer and decoder architecture without major rework.
- Inventory additional candidates, including TIFF, and verify loader availability, target-distribution codec dependencies, rendering correctness, and memory behavior before enabling each format.
- Define still-frame or page selection behavior for animated and multi-page inputs before enabling them; full animation playback is a separate scope decision.
- Treat RAW as a separate research effort because it requires preview policy, color processing, and heavier dependencies.

### Input customization

- Extend shortcut editing beyond core viewer commands if users need it.
- Add shortcut profiles, import/export, or scopes only after concrete demand establishes their semantics.

### Distribution

- Establish application versioning and a documented version-bump and release-tag policy, keeping the CMake version, application version reporting, and package versions aligned.
- Add CI build and test jobs, then automate tagged releases with versioned artifacts and checksums.
- Prepare release packaging for Arch Linux, Debian-based distributions (`.deb`), and RPM-based distributions (`.rpm`), with documented build/install workflows and validated dependencies.
- Build on the existing Arch Linux `insidious-git` recipe and add a separate tagged-release `insidious` recipe with an immutable source archive and real checksum when a release tag exists.
- Design desktop integration, icons, and MIME associations without weakening the recent-item privacy policy.

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
