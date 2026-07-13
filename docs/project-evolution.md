# Project Evolution

This document preserves the reasoning behind the project’s initial scope and explains how that foundation grew into the current application. It is historical context, not a description of the product’s present maturity.

## Initial foundation

The project began with a deliberately narrow viewer pipeline. The first goal was to prove that the application could:

- open a supported image or folder;
- use a selected image’s parent folder as browsing context;
- navigate in natural filename order;
- render through libvips without blocking the UI;
- provide fit, actual-size, zoom, pan, view-only rotation, and fullscreen controls;
- handle unreadable or corrupt entries without losing the collection;
- keep decoded data and browsing state out of persistent storage.

JPEG, PNG, WebP, BMP, and DIB formed the explicit image allowlist. Archive support, thumbnails, richer metadata, comparison, configurable input, and advanced rendering were intentionally separated until the source and decode abstractions were reliable.

That narrow scope established several principles that still define the project: UI/core separation, asynchronous work, collection-source abstraction, memory-only caches, no disk extraction during normal archive viewing, and predictable navigation after errors.

## Expansion into a complete viewer

The application has moved well beyond that initial foundation. The current implementation adds:

- lazy filesystem and archive source trees with active-folder navigation;
- ZIP, 7z, and RAR browsing through libarchive, including in-memory passwords;
- homogeneous multi-image, multi-folder, and multi-archive sources;
- lazy, asynchronous, memory-only thumbnails;
- Row, Grid, Carousel, and two-image Hover Switch comparison;
- clean comparison fullscreen and configurable viewer fullscreen overlays;
- quick filename jump, runtime sorting, original-byte clipboard copy, and EXIF information;
- configurable viewer behavior, scaling quality, color conversion, region rendering, interface theme, and core keyboard or mouse shortcuts;
- HiDPI-aware rendering, decode coalescing, guarded prefetch, region-sized non-fit rendering, and explicit render-memory checks;
- automated core and UI-focused tests plus developer memory-audit tooling.

These are integrated product capabilities rather than experiments around a minimal pipeline.

## What remains intentionally bounded

The application still keeps explicit boundaries. GIF animation, TIFF, AVIF, JPEG XL, RAW, persistent history, persistent thumbnail or comparison state, archive password persistence, display-profile matching, advanced metadata backends, and broad shortcut profiles are not part of the current product.

These boundaries do not imply an unfinished core. They protect performance, privacy, dependency discipline, and predictable behavior while future work is evaluated independently. Current priorities and research directions are tracked in the [roadmap](roadmap.md).

## Durable lessons

- Treat files, folders, archives, and temporary selections as collection sources instead of scattering source-specific branches through the viewer.
- Keep long-running I/O and decode work away from the UI thread and reject stale results explicitly.
- Prefer display-sized or region-sized rasters and bounded memory caches over full-collection decoding.
- Preserve the active collection when one entry fails; let the user decide how to continue.
- Treat privacy rules as architecture constraints, not presentation claims.
- Add formats and persistent state only through explicit, documented product decisions.
