# Requirements

This document defines observable product behavior and non-negotiable engineering constraints. The [user guide](user-guide.md) explains how to use these capabilities; the [implementation map](implementation.md) describes how they are built.

## Image sources

The application must open:

- one supported image file;
- multiple selected image files as a temporary flat collection;
- one filesystem folder;
- multiple filesystem folders under a synthetic root;
- one supported archive;
- multiple supported archives under a synthetic root;
- the same source types through drag and drop;
- an optional source path supplied on the command line.

Opening one image must use its parent folder as browsing context while selecting the requested file as the current entry. Multiple-source operations must be homogeneous: mixed images, folders, and archives, or sets containing unsupported paths, must fail as a whole rather than opening partially.

## Viewing and rendering

The viewer must provide:

- fit-to-window and actual-size base modes;
- incremental zoom and pan;
- right-drag rectangle zoom;
- view-only 90-degree rotation in both directions;
- optional EXIF orientation;
- normal and fullscreen viewing;
- a reset to the configured base mode;
- a configurable scaling-quality preset;
- optional embedded ICC conversion to sRGB;
- configurable region-based rendering for non-fit views.

Actual size uses physical-pixel semantics on HiDPI displays: one source image pixel maps to one physical screen pixel, while widget geometry, pointer input, overlays, and pan offsets remain in Qt logical pixels.

Fit rendering should request a display-sized raster. Non-fit zoom and actual-size rendering should request the visible region plus overscan by default. Disabling region rendering may request a full scaled raster for smoother panning, but it must use the same render-memory guard and failure path.

The scaling-quality preset, color-management mode, EXIF orientation, view rotation, output geometry, and device pixel ratio must participate in the decode request and cache identity whenever they can change the output raster.

## Navigation

The viewer must support previous, next, first, and last image commands; explicit previous and next folder commands; list and thumbnail selection; and quick jump by filename.

Previous and next image navigation operate inside the active folder. At a folder boundary they continue to the previous or next suitable folder in source-tree order. First and last remain scoped to the active folder. Explicit folder navigation selects the first image in the destination folder after the current runtime sort is applied.

By default, changing images resets zoom and pan to the configured fit or actual-size base mode. Preferences may change this to preserve the current view. View-only rotation always resets when the current image changes.

If a previous or next command cannot move to another image or folder, it must still apply the image-switch policy and reload the current entry. This lets the user recover from temporary render errors without changing the collection.

Quick Jump must search display names case-insensitively inside the active folder only. It must not scan the filesystem, decode images, read metadata, or persist search state.

## Mouse interaction

Default wheel mappings are:

- wheel: navigate images;
- `Ctrl+wheel`: zoom around the cursor;
- `Shift+wheel`: no action.

Each mapping is configurable as image navigation, zoom, or no action. Wheel, toolbar, and shortcut zoom must use the same configurable step; toolbar and shortcut zoom are anchored at the viewport center.

The default left-button mode uses a simple click to enter temporary zoom relative to the current displayed scale and a later click to restore the fit or actual-size base view after click or rectangle zoom. Left drag pans only outside the base fit view.

The alternative left-button mode starts temporary zoom on press, pans while dragged, and restores the exact previous view on release. Middle-button drag or `Ctrl+left` drag pans any non-fit view whose displayed image is larger than the viewport without starting zoom.

Right-button drag defines a zoom rectangle. Left double click toggles fullscreen and must not also trigger simple-click zoom. A configured mouse-button shortcut takes priority over a built-in gesture using the same input.

The idle magnifier cursor must appear only when the selected left-button zoom or restore action is available: a decoded image is visible, no render is pending, and hold zoom is not active.

## Source tree and file lists

The `Files` panel must contain a folder tree and a list of direct images in the active folder. Selecting a folder changes the active viewer collection.

Filesystem children must load lazily on selection or expansion. A loaded branch must be inserted into the existing tree model without resetting unrelated branches. Archive trees are built asynchronously from headers without reading image payloads.

Multiple selected images use one flat synthetic root. Multiple folders or archives use top-level roots in input order. Folder ordering and top-level source ordering are not changed by image sorting.

## Sorting

Image sorting is runtime-only and applies to the active-folder image list. Supported keys are natural name, type, size, and date, each in ascending or descending order.

Every process starts with natural name ascending. The selected runtime mode applies to folders and sources opened later in the same process but must not be written to settings. A temporary selected-file collection initially preserves input order until the user explicitly applies a sort.

Missing archive dates must have deterministic placement and must not make ordering unstable.

## Supported formats

The explicit image allowlist is:

- JPEG (`.jpg`, `.jpeg`);
- PNG (`.png`);
- WebP (`.webp`);
- BMP and DIB (`.bmp`, `.dib`).

The explicit archive allowlist is:

- ZIP (`.zip`);
- 7z (`.7z`);
- RAR (`.rar`).

GIF, TIFF, AVIF, JPEG XL, and RAW are not currently supported. A format must not become user-visible solely because the system libvips build exposes a decoder for it.

## Archives

Archives must behave as virtual folder trees. Non-image entries are ignored. Image display reads must not extract entries to disk or require a persistent full-entry buffer.

Password-protected archives must prompt when necessary. Passwords stay only in memory for the current source and are removed when that source is replaced or the process exits. Opening multiple archives asks once and applies the password to the selection; if any later archive rejects it, the multi-archive open fails as one operation.

Operations that require complete original bytes, such as clipboard copy, may use a separate bounded in-memory full-entry read. They must still avoid disk extraction and persistent state.

## Thumbnails

The thumbnail tab is optional and scoped to the active folder. Thumbnail work starts only while the tab and its containing panel are visible.

Visible and nearby thumbnails must be decoded lazily and asynchronously. The queue and memory-only cache must be invalidated when the source changes, the panel closes, or the tab is no longer active. Thumbnail state must never be persisted.

## Image comparison

Users may build a temporary comparison session from images in the current source, including images selected from different folders over time. Selected list or thumbnail items may also be dropped onto the visible Compare tab through an internal source-scoped drag operation.

Row, Grid, and Carousel support one to eight images. Hover Switch requires exactly two. Row and Grid provide per-image zoom and pan; Carousel provides a single cyclic viewport; Hover Switch uses shared zoom and pan. Supported runtime-only alignment presets are Fit Center, Top Left, Top Center, Center Left, Bottom Left, Bottom Center, Same Source Scale, and Cover Center.

Row, Grid, and Carousel allow removal; Row and Grid also allow reordering. A mode must reject an append that would violate its item-count constraints rather than silently changing modes or dropping images.

Comparison decoding must be asynchronous and memory-only. Sessions, item order, selected paths, alignment, decoded rasters, and comparison cache state must not be persisted. Changing the source clears the session.

Comparison fullscreen is separate from viewer fullscreen. It preserves mode, alignment, order, zoom, and pan while hiding all application chrome and viewer edge overlays. `F11`, `Esc`, or left double click on the comparison layout leaves fullscreen.

## Information and clipboard

The Info panel must display existing `ImageEntry` and decode data only: name, format, path, file size, modified time, decoded dimensions, color-profile state, and a curated set of EXIF fields supplied by the decoder. It must not initiate separate filesystem scans or metadata backends.

Copy Original Image must place original source bytes on the clipboard with an appropriate MIME type and may add compatible image data decoded from those bytes. It must never copy the transformed display raster. Filesystem and archive reads run away from the UI thread, use a bounded read, and reject stale results if the current entry changes.

## Fullscreen

Viewer fullscreen hides the normal menu bar, toolbar, statusbar, Files dock, and Info dock. Left, right, top, and bottom controls appear as overlays over the image and must not resize the viewer, change zoom or pan, request a decode, or cause visible flicker.

Default trigger zones are 7% of window width for side overlays and 5% of window height for top and bottom overlays. Side panels take priority in corners. A separately configurable pinned information line remains independent of the edge overlays.

If the window manager forces the application out of fullscreen, normal chrome must be restored before layout state is saved. Normal `saveState()` data must never be captured while fullscreen UI state is active.

## Preferences and shortcuts

Preferences include viewer defaults, image-switch behavior, left-button zoom behavior, EXIF orientation, click zoom multiplier, wheel mappings, zoom step, scaling quality, embedded color conversion, region rendering, fullscreen trigger zones and information-line presentation, normal-mode theme, layout persistence, and core viewer shortcuts.

Shortcut bindings use stable command identifiers and portable keyboard or mouse input strings. Empty bindings are valid. Apply and OK must reject duplicate non-empty inputs in the editable command set. Quit, dock toggles, profiles, import/export, and configurable shortcut scopes remain outside the current shortcut editor.

Unknown or malformed stored values must fall back to safe defaults, and numeric settings must be clamped to their supported range.

## Performance and concurrency

Folder scans, archive enumeration, image decoding, metadata extraction, original-byte reads, thumbnail generation, comparison decoding, and prefetch must not run on the UI thread.

Every asynchronous UI update must be guarded against stale source, entry, or decode state. Same-entry render requests should coalesce to the newest pending request, while switching to another image starts a new foreground decode immediately.

Neighbor prefetch is limited to fit-sized requests, tracked by cache key, and guarded by cache generation. It must not start for actual-size or custom zoom renders or for password-protected archive entries.

Decoded images and thumbnails use bounded memory-only caches. Render-memory estimates must use the shared core budget logic. A `QImage` returned from decoder-owned memory must own its buffer through a cleanup callback or be a real copy.

## Loading and errors

Navigation to an uncached image keeps the previous raster visible. A subtle loading overlay appears only after a short delay so fast decodes do not flash. Zoom-triggered renders also preserve the previous raster and decoded state but do not display a loading label or move the old raster prematurely.

Additional zoom or view-transform requests must not stack while a render is pending; navigation may cancel the pending render. If a zoom render exceeds the memory budget, the previous raster, zoom, and pan must be restored and a compact error overlay shown.

Unsupported, missing, unreadable, corrupt, empty, password-rejected, or oversized sources must produce a clear error without crashing. Failure of the current entry must preserve the collection and keep previous/next navigation available. Failed results must not enter caches.

## Privacy and traces

Normal use must not persist:

- opened file, folder, or archive history;
- source paths or a default image path;
- thumbnail or decoded-image caches;
- comparison sessions or selected comparison paths;
- archive passwords;
- archive extraction output;
- image data;
- file log destinations or file logs.

Qt settings may store non-history preferences and interface geometry, dock state, and splitter positions. File dialogs use the non-native Qt implementation to avoid implicit desktop recent-item integration until a separate privacy decision changes that policy.
