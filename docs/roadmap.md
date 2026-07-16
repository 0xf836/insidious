# Roadmap

This roadmap is a living statement of direction, not a release schedule or compatibility promise. Current capabilities are documented in the [README](../README.md), [user guide](user-guide.md), and [project evolution](project-evolution.md).

## Next

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

- Evaluate AVIF and JPEG XL against target-distribution codec availability and memory behavior.
- Evaluate TIFF only if libvips behavior is reliable across supported environments.
- Treat RAW as a separate research effort because it requires preview policy, color processing, and heavier dependencies.

### Input customization

- Extend shortcut editing beyond core viewer commands if users need it.
- Add shortcut profiles, import/export, or scopes only after concrete demand establishes their semantics.

### Distribution

- Publish a tagged-release `insidious` recipe with an immutable source archive and real checksum when a release tag exists.
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
