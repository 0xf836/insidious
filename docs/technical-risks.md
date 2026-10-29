# Technical Risks

This document tracks unresolved engineering risks and current mitigations. Accepted product behavior belongs in [requirements](requirements.md), and completed architectural choices belong in [decisions](decisions.md).

## Archive access performance

ZIP, 7z, and RAR expose different access characteristics through libarchive. Display decode streams one selected entry without disk extraction, but repeated navigation may still require expensive header traversal or decompression, especially for solid archives.

Current mitigation: archive scans and decodes run asynchronously; password-protected archive entries are excluded from speculative neighbor prefetch. Large real-world archives still require format-specific benchmarks before access or prefetch policy changes.

## Total process memory

The render-memory guard limits the requested output raster, not total peak memory. libvips working memory, source-reader buffers, the displayed raster, decoded caches, thumbnails, comparison rasters, and concurrent jobs are additive.

Current mitigation: display-sized fit rendering, region-sized non-fit rendering by default, bounded memory-only caches, fit-only prefetch, cache-generation guards, and the separate [total peak memory audit](memory-audit.md). Thumbnail audit coverage must be added before thumbnail cache or preload limits increase.

## Large and changing filesystem trees

Lazy folder loading prevents a complete recursive scan on open, but very large individual directories can still produce expensive enumeration and model updates. Files may also be renamed, removed, or become unreadable while a source is active.

Current mitigation: scans run off the UI thread, stale results are rejected, loaded branches use targeted insertion, and current-entry failures preserve navigation. Partial streaming of very large directory results and live filesystem reconciliation are not implemented.

## Codec and color-management variability

Actual decode behavior depends on the libvips build and its optional loaders. Embedded ICC conversion also depends on usable lcms2 support and valid source profiles.

Current mitigation: an explicit format allowlist prevents accidental feature exposure; profile failures fall back without failing the entry; the build requires libvips/vips-cpp 8.18.3 or newer. The [dependency audit](dependency-audit.md) records missing SIMD resize bounds fixes in Ubuntu 26.04's 8.18.0 package and the performance cost of disabling SIMD. Target-distribution coverage must be checked before adding formats or packaging.

## Fullscreen behavior across window managers

Fullscreen, maximized, and tiled state transitions are not identical across Linux window managers. A lower-edge artifact has been observed after leaving dark-theme fullscreen under i3wm when the window spans the screen width.

Current mitigation: the application stores and restores the previous non-fullscreen state, performs cleanup on forced fullscreen exit, and avoids saving normal layout state while fullscreen UI mode is active. The remaining artifact is tracked in the [roadmap](roadmap.md).

## Input customization conflicts

Mouse buttons can represent both viewer gestures and configurable commands. Broadening shortcut customization could introduce scope and priority ambiguity.

Current mitigation: configured mouse shortcuts take priority over built-in gestures, duplicate non-empty inputs are rejected, and customization is limited to a defined core command set. Profiles, import/export, and configurable scopes require separate semantics before implementation.

## Advanced formats

AVIF and JPEG XL depend on codec availability and memory behavior across target systems. TIFF has a wide variety of layouts and sizes. RAW is a family of formats requiring preview policy, color processing, and significant dependency decisions.

Current mitigation: these formats remain outside the explicit allowlist and are treated as separate research items rather than opportunistic decoder additions.
