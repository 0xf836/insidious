# Architecture

This document describes stable system boundaries and design rules. Concrete classes, files, build targets, and current control flow are documented in the [implementation map](implementation.md).

## Core principle

The viewer must not depend on the physical origin of an image. A filesystem folder, selected file list, archive, or synthetic multi-source root exposes image entries through collection semantics. The Files panel additionally consumes a source tree whose selected folder becomes the active viewer collection.

Source-specific reading stays behind core source and archive code. Widgets receive entries, models, decoded images, and explicit status or error results; they do not scan folders or parse archives themselves.

## Dependency direction

The application is divided into these layers:

1. process bootstrap;
2. Qt UI and orchestration;
3. viewer and settings state;
4. collection sources and archive access;
5. image decoding and metadata extraction;
6. render-memory estimation and memory-only caches.

`src/ui` may depend on core types. `src/core` must remain free of `QWidget` and other widget-layer dependencies. QtCore and QtGui value types such as `QString`, `QImage`, `QSize`, and `QDateTime` are accepted in core where they express application data.

## Application bootstrap

The bootstrap layer initializes libvips, configures process-wide image cache limits, creates `QApplication`, parses the optional source argument, constructs the main window, and owns clean process shutdown.

It must not absorb source scanning, viewer behavior, or window-specific command routing.

## UI and orchestration

The UI layer owns widgets, actions, shortcuts, drag and drop, menus, toolbars, status presentation, fullscreen overlays, and pointer or keyboard event translation.

`MainWindow` is the current orchestration boundary. It classifies open requests, starts asynchronous jobs, tracks source and decode generations, routes commands through viewer state, and applies successful results to widgets.

UI models remain presentation-only:

- the collection tree model exposes source folders and supports targeted lazy-branch insertion;
- the image list model exposes direct entries in the active folder;
- thumbnail support requests asynchronous memory-only previews only while visible;
- comparison support owns temporary comparison state and decode work without changing the single-image viewer collection;
- the info panel displays entry and decoder-provided metadata without reading sources.

## Viewer state

Viewer state owns the active collection position and view state:

- current entry index;
- fit, actual-size, or custom zoom mode;
- zoom factor and pan offset;
- view-only rotation;
- previous, next, first, and last operations.

Cross-folder navigation is orchestrated above the collection controller because it changes the active folder collection. View rotation resets when the entry changes. The configured image-switch policy decides whether zoom and pan reset or are preserved.

## Collection sources

### Source tree

A source tree contains folder nodes with direct image entries and child folders. Selecting a node supplies its direct images to viewer state.

- Filesystem nodes carry enough identity for lazy child loading.
- Archive nodes are built from asynchronous header enumeration.
- Several selected images use one flat synthetic root.
- Several folders or archives use ordered top-level roots under a synthetic root.

Source paths in a multi-source tree are namespaced so equal relative folder names from different inputs do not collide.

### Image entries

An image entry carries the information needed to identify, display, validate, and read one image:

- display name and source-relative path;
- filesystem path or archive path plus member path;
- source kind and format hint;
- size and modification time when available.

Archive passwords are held separately as source runtime state keyed by archive path. They must not enter image entries, labels, cache keys, settings, clipboard metadata, or persistent records.

### Filesystem sources

Opening one image creates an immediate selected entry and scans its parent folder asynchronously. Opening a folder loads the root and enough child structure to populate the source tree without recursively scanning the entire hierarchy.

Expanding or selecting an unloaded folder starts a targeted asynchronous load. The result updates only that branch if the source generation and node identity still match.

### Archive sources

libarchive enumerates ZIP, 7z, and RAR headers and groups supported image members into virtual folders. Display decode reads the selected member through a sequential stream without extracting it or buffering the complete member first.

A separate bounded full-entry read exists for features that require original bytes, such as clipboard copy. This path also stays in memory and does not extract to disk.

### Multi-source operations

Open and drop requests are classified before any source is committed. The operation proceeds only when all paths are supported and belong to one category: images, folders, or archives.

This atomic classification avoids partial opens and gives multi-archive password handling one clear success or failure boundary.

## Decode pipeline

A decode request describes the entry and every property that can change its output raster: view mode, requested scale or region, viewport, pan, device pixel ratio, EXIF orientation, view rotation, scaling quality, color-management mode, and region-rendering policy.

The libvips decoder:

1. validates and opens the filesystem file or archive stream;
2. reads source dimensions and selected EXIF strings;
3. applies optional EXIF orientation and view rotation;
4. calculates a display-sized or region-sized request;
5. checks the shared output-raster memory budget;
6. resizes with the selected kernel;
7. converts an embedded profile to sRGB when enabled and available;
8. normalizes pixels to an 8-bit RGB or RGBA layout;
9. returns a `QImage` that owns its backing buffer.

Profile conversion failure is non-fatal and falls back to an sRGB assumption. Display-profile matching is a separate future layer and is not implied by embedded-profile conversion.

## Caching and prefetch

Decoded viewer images use a bounded memory-only cache keyed by entry identity and render properties. Thumbnail and comparison caches are separate, bounded, and also memory-only.

Neighbor prefetch is limited to fit-sized requests. Jobs are tracked and coalesced by cache key. Every write carries the cache generation captured at job start, preventing a stale job from repopulating a cache after a source change or clear.

Actual-size, custom zoom, and password-protected archive entries do not start speculative prefetch.

The render-memory budget estimates output raster memory only. libvips working memory, source buffering, displayed images, caches, thumbnails, and concurrent tasks contribute to total process peak memory and are measured separately by the [memory audit](memory-audit.md).

## Asynchronous work and stale results

Filesystem scans, archive enumeration, image decoding, thumbnail generation, comparison decoding, original-byte reads, and prefetch run away from the UI thread.

Every job captures the identity needed to validate its result. `MainWindow` uses source and decode generations; models and secondary loaders use equivalent generation or node checks.

Same-entry rerender requests coalesce to the newest pending request. A switch to another entry starts a new foreground decode immediately and invalidates older display results. Cache hits update the UI immediately while still invalidating obsolete active decodes.

Navigation preserves the previous raster until the new foreground result is ready. Zoom renders also preserve the old raster and restore the complete previous view if the new request exceeds the render budget.

## Fullscreen architecture

Viewer fullscreen overlays are child widgets placed above the central image view, not dock widgets or layout participants. The left and right overlays mirror the normal Files and Info panels; the top overlay reuses normal `QAction` objects; the bottom overlay uses the shared status formatter.

Overlay visibility must not change viewport geometry or initiate a decode. Compare fullscreen follows a separate content path that hides all application chrome and viewer overlays while preserving comparison state.

Fullscreen entry stores the previous non-fullscreen state and chrome visibility. Cleanup also runs when a window manager forces fullscreen exit. Layout persistence must not capture normal window state until fullscreen UI state has been fully restored.

## Settings

The core settings schema owns defaults, stable command identifiers, serialization, validation, clamping, and category reset behavior. The Preferences dialog is a UI editor over that schema.

Stored settings include viewer behavior, rendering preferences, core shortcuts, fullscreen presentation, normal-mode theme, and optional Qt geometry or layout state. Runtime sort mode, opened sources, passwords, thumbnails, decoded data, and comparison sessions are not settings.

## Privacy boundary

Privacy requirements are architectural constraints:

- no application-owned recent-item history;
- no persistent thumbnail, decoded-image, prefetch, or comparison cache;
- no persistent comparison session or selected paths;
- no archive extraction during normal viewing;
- no archive password persistence;
- no file logging during normal use;
- no viewed paths in settings.

Non-native Qt file dialogs avoid implicit desktop recent-item integration. Changing that behavior requires an explicit architecture and privacy decision.
