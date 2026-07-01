# AGENTS.md

## Project

This is a native Linux GUI image viewer. The application should quickly open and browse image files, folders, and archives containing images. The project prioritizes performance, predictable navigation, rendering quality, and privacy: no activity history, no persistent thumbnail cache, no persistent archive extraction, and no file logs in normal usage.

## Language policy

- Source code, identifiers, internal comments, commit messages, and developer-facing implementation notes should be in English.
- Product requirements in `docs/` may be in Russian.
- User-visible UI strings should be prepared for localization; do not hardcode Russian UI strings deep inside core logic.

## Working rules

- Do not implement large features in one step.
- Before coding, read the relevant files in `docs/` and the current implementation.
- Prefer small, reviewable diffs.
- Do not rewrite unrelated code.
- Do not introduce new dependencies without explaining why they are needed.
- Keep `docs/`, `AGENTS.md`, and `README.md` up to date.
- Any feature addition, change, or removal must be reflected in the relevant documentation.
- Any change to project structure, architecture, layering, dependencies, or implementation flow must be reflected in the relevant documentation.
- Keep GUI code separate from core image, collection, archive, metadata, and cache logic.
- Keep long-running work off the UI thread.
- Add or update tests for non-trivial core logic.
- After changes, run the relevant build/test command if it exists.
- If a requirement is ambiguous, stop and ask for clarification or write down the assumption before implementing.

## Current implementation quick map

The current MVP implementation is C++20 with Qt6 Widgets, libvips/vips-cpp, and libarchive.

- `CMakeLists.txt` defines the build, `insidious_core`, the `insidious` GUI executable, and `insidious_core_tests`.
- `src/app/main.cpp` owns process bootstrap: initializes libvips, filters known missing-dependency warnings from optional libvips modules outside the MVP format set, configures libvips cache limits, creates `QApplication`, parses an optional path argument, and shows the main window.
- `src/ui/` is the Qt UI layer. Keep direct widget, action, shortcut, drag'n'drop, statusbar, fullscreen, and paint/event handling here.
- `src/ui/main_window.*` coordinates UI commands, async folder scans, async image decode jobs, stale-result generation counters, cache lookup, and neighbor prefetch.
- `src/ui/settings_dialog.*` is a UI-only Preferences dialog. Keep validation, persistence schema, and defaults in `src/core/app_settings.*`.
- `src/ui/image_info_panel.*` is a UI-only metadata panel for the current entry, decoded dimensions, and EXIF fields supplied by the decoder. Keep EXIF parsing and metadata backends out of this widget.
- `src/ui/collection_tree_model.*` is a UI-only tree model for folders in the current collection source.
- `src/ui/image_list_model.*` is a UI-only list model for images in the active folder. Keep it source-agnostic and free of thumbnail/decode work.
- `src/ui/quick_jump_dialog.*` is a UI-only filename jump dialog over the current collection. Keep it free of filesystem scanning, decoding, metadata lookup, and persistent state.
- `src/ui/image_view_widget.*` paints the current `QImage` and translates mouse input into viewer commands. It should not scan folders or decode images.
- `src/core/` is non-widget logic: image entries, supported format filtering, natural sorting, folder/archive scanning, viewer state, settings schema, decode request/result types, render-memory budget estimation, memory cache, libarchive full-entry/stream reading, and libvips decoding.
- `tools/memory_audit.cpp` and `tools/memory_audit.py` are developer-only total peak memory audit tooling. They generate temporary synthetic fixtures and must not introduce persistent app data or user-facing settings.
- `tests/core_tests.cpp` covers core behavior with Qt Test. Add tests here for non-trivial core changes.
- `docs/implementation.md` is the longer orientation document for current code structure and data flow.

## Build and test

Use an out-of-source build. The default local build directory is ignored by git.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

For headless runs, use `QT_QPA_PLATFORM=offscreen`. CTest already sets this for the core test executable.

## Architectural principles

- Separate the app into clear layers:
  - application bootstrap
  - UI layer
  - viewer/controller state
  - collection sources
  - image loading/decoding
  - metadata reading
  - archive access
  - memory cache
  - settings
- Treat a folder, a user-selected file list, and an archive as different implementations of the same collection/source concept.
- The viewer should not care whether the current image comes from a filesystem folder, a dropped file list, or an archive.
- Avoid blocking the UI during folder scanning, archive reading, image decoding, metadata reading, or thumbnail generation.
- Prefer lazy loading and prefetching over loading the whole collection into memory.

## Privacy and traces

- Do not add persistent history of opened files, folders, or archives.
- Do not add persistent thumbnail cache by default.
- Do not extract archives to disk in normal mode.
- Do not store archive passwords.
- Do not write file logs in normal mode.
- Settings are allowed, but they must not include recently opened paths or viewed items unless explicitly requested later.
- Settings must not include archive paths, default image paths, passwords, thumbnails, decoded image data, or log destinations unless explicitly requested later and documented as a privacy change.

## MVP boundaries

Current implementation should stay focused on a stable viewer pipeline for regular filesystem images, folders, archives, navigation, fullscreen, metadata, configurable core viewer shortcuts, and privacy-safe settings. RAW support, advanced metadata, advanced shortcut features, password persistence, and thumbnail panels are later milestones unless explicitly requested.

Current MVP supports regular filesystem images, filesystem folder trees with lazy per-folder loading, ZIP/7z/RAR archive trees through libarchive without disk extraction, and a basic Preferences dialog. Password-protected archives prompt on open and keep the password only in memory for the current archive source. The enabled image extension filter is JPEG, PNG, WebP, BMP, and DIB. GIF, TIFF, AVIF, JPEG XL, RAW, password persistence, thumbnails, advanced metadata backends, advanced shortcut features, configurable/fuzzy quick jump, and recent-file history are intentionally out of scope.

Use `docs/roadmap.md` as the living backlog. Keep completed work, next stabilization tasks, mid-term features, later milestones, and explicit non-goals sorted there when product direction changes.

Current MVP mouse behavior:

- Wheel navigates previous/next image by default; wheel, `Ctrl+wheel`, and `Shift+wheel` actions are configurable as navigate, zoom, or no action.
- `Ctrl+wheel` zooms in/out relative to the current displayed scale by default, including the current fit scale, anchored at the cursor position.
- `Shift+wheel` has no action by default and is reserved for explicit user configuration.
- Next/previous actions reset zoom and pan to the current base mode (`fit to window` or `actual size`) by default, then reload the current entry. At an active-folder boundary, they continue to the next/previous suitable source-tree folder before treating the source as exhausted. The basic settings UI can switch image navigation to preserve the current view instead.
- Explicit previous/next folder actions jump to the previous/next suitable source-tree folder when one exists and select the first image in that folder after the current runtime sort is applied. They use the same adjacent-folder discovery as image navigation at folder boundaries and default to `MouseBack` / `MouseForward`.
- Left button zoom behavior is configurable. The default `ToggleOnClick` mode uses simple left click to enter temporary zoom relative to the current displayed scale, centered on the clicked image point; the next click after click/rectangle zoom restores fit or actual size. `HoldToZoomPan` starts temporary zoom immediately on left press, left-drag pans the zoomed raster, and release restores the exact previous view.
- In left-button zoom modes, show the idle magnifier cursor only while zoom/restore or hold-to-zoom is available: a decoded image is visible, no render is pending, and hold zoom is not active.
- Toolbar, shortcut, and wheel zoom use the same configurable zoom step. The default is 25%. Toolbar and shortcut zoom anchor at the viewport center.
- Rotate left/right is a view-only transform exposed through `View`, the normal toolbar, the fullscreen top overlay, and configurable shortcuts. It applies after EXIF autorotate, participates in the decode cache key, does not change copied original image bytes, and resets when the current image changes.
- Core viewer shortcuts are configurable in Preferences through stable command ids stored in `AppSettings`. The first supported set covers open file/folder, navigation, quick jump, copy original image bytes, Preferences, zoom/fit/actual size, rotate, EXIF orientation, and fullscreen. Commands may have keyboard shortcuts and mouse button shortcuts at the same time. Empty shortcuts are allowed, duplicate non-empty shortcuts must be rejected before save, and `Quit` plus dock toggles remain static for now.
- Normal mode supports a persistent light/dark theme setting. Fullscreen chrome remains dark-only.
- In default left-button mode, hold left mouse button and drag pans the image only outside base `fit to window`. When the base mode is `fit to window` but the current image has already been zoomed into a larger custom view, hold middle mouse button or `Ctrl+left mouse button` to pan the enlarged image.
- Hold right mouse button and drag a rectangle to zoom to that selected area.
- Double click toggles fullscreen and must not also trigger temporary click zoom.

Current MVP fullscreen behavior:

- In normal mode, the main menu bar is hidden by default and appears transiently for Alt-driven menu navigation; the normal toolbar also has a right-aligned compact app menu button, and the fullscreen toolbar keeps the same button inside its main command cluster so the right edge overlay does not cover it. Toolbars, shortcuts, and menu actions must continue to share the same `QAction` objects where practical.
- Fullscreen hides the normal menu bar, toolbar, statusbar, `Files` dock, and `Info` dock by default, leaving only the image area visible.
- Edge panels appear as overlays over the image: left `Files`, right `Info`, compact dark top actions toolbar, bottom status text.
- Normal statusbar and fullscreen bottom status text share the compact `status_line_formatter` output: labelled dimensions/MP, file size, modified date, taken date, display path, zoom, and index/count. The pinned fullscreen info line is a separate overlay with the `name (resolution | filesize) [index/count] zoom` field set and configurable visibility/font/size/color.
- Default fullscreen trigger zones are 7% of window width for side panels and 5% of window height for top/bottom panels. They are stored in app settings.
- Side panels have priority in fullscreen corners.
- Showing or hiding fullscreen overlays must not resize the image viewport, alter zoom/pan, call image reload/decode, or cause visible image flicker.

## Implementation constraints

- Keep `src/core` free of `QWidget`/GUI dependencies. QtCore/QtGui value types such as `QString`, `QImage`, `QSize`, and `QDateTime` are currently accepted there.
- Do not run folder scanning or libvips decoding on the UI thread.
- Guard async UI updates against stale results when the current source or decode request changes.
- Main image decode requests must go through the `MainWindow` loader. Re-render requests for the same entry should coalesce to the newest pending request, while image switches should start the new foreground decode immediately.
- Keep cache state memory-only. Do not introduce persistent cache files, recent-file lists, or viewed-path settings.
- Prefetch cache writes must use `MemoryImageCache` generation guards so stale background prefetch cannot repopulate cache after source switches or cache clears.
- Prefetch jobs should be tracked/coalesced by cache key and limited to fit-sized decode requests. Do not start speculative prefetch for actual-size/custom zoom requests or password-protected archive entries.
- `Copy Original Image` must copy original source bytes to the clipboard and may add compatible clipboard image data decoded from those same bytes, but it must not use the rendered raster. Filesystem and archive reads must stay off the UI thread; clipboard archive entries use bounded full-entry reads, display decode uses streaming archive reads, neither extracts to disk, and archive passwords must remain only in memory.
- When adding, changing, or removing a setting, update `src/core/app_settings.*`, `src/ui/settings_dialog.*`, `docs/`, `README.md`, and this file together.
- Failed current entries must not destroy collection/navigation state. Show a clear error and keep next/previous navigation available.
- Transient loading during image navigation should preserve the currently displayed raster and use a delayed subtle overlay so fast decodes do not flash a loading label. Zoom-triggered loading should preserve the displayed raster and decoded state without showing a loading label or moving the old image. Do not stack additional zoom/view-transform requests while an image render is pending; navigation may still cancel the pending render. Zoom-triggered render-too-large errors should restore the previous zoom/pan/raster and show a compact overlay instead of clearing the image. Error, empty, and scanning states may clear the image and show a full message.
- Prefer display-sized rasters for `fit` and region-sized rasters for non-fit zoom/actual-size rendering by default. The viewer setting `regionRenderingEnabled` may disable region rendering for smoother panning at higher memory cost; this must still go through the shared render-memory budget and fallback path.
- Use `src/core/render_memory_budget.*` for render-memory checks. Do not duplicate ad hoc raster byte math in decoders, cache code, or UI code.
- Decoder-owned `QImage` data must either own its backing buffer through a Qt cleanup callback or be a real copy; never return a `QImage` pointing at freed libvips memory.
- EXIF autorotate is a viewer setting and a synced checkable `View`/toolbar action. It must flow through `DecodeRequest` and the decoded image cache key so autorotated and non-autorotated rasters never collide.
- If adding a source type later, make it look like a collection of `ImageEntry` objects to the viewer/controller rather than special-casing it throughout UI code.
- The left `Files` panel is split into a folder tree and an active-folder image list. Selecting a folder changes the active viewer collection to direct images in that folder only.
- File sorting is runtime-only and applies only to the active-folder image list: default on app start is natural name ascending; users can switch name/type/size/date asc/desc for the current process; the selected runtime mode also applies to newly opened sources, but it must not be persisted in settings.
- Next/previous navigation should traverse source-tree folders at active-folder boundaries; first/last remain scoped to the active folder.
- Filesystem folder nodes should be loaded lazily on selection/expansion. Do not reintroduce full recursive filesystem scans for normal folder opening.
- Archive folder trees are built from async header enumeration; do not extract archives to disk or read image data before decode.
- The left file list should mirror the active folder collection only. Do not put source scanning, archive access, decoding, thumbnails, or persistence there.
- The right info panel should display existing entry/decode data only. EXIF currently comes from libvips metadata collected during decode; do not add separate metadata backends or persistent panel state until those milestones are explicitly planned.
- If adding dependencies, update `README.md`, `docs/decisions.md`, and this file with why they are needed.

## Definition of done

A task is done when:

- The requested behavior is implemented and scoped to the task.
- The app still builds/runs.
- Relevant tests are added or updated where practical.
- Error handling is present for common failure modes.
- The final response explains what changed, how it was tested, and what was intentionally left out.
