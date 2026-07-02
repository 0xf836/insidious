# Current implementation map

This document describes the current codebase shape for future coding agents. Product requirements remain in the other `docs/` files; this file is about the implementation that exists now.

## Stack

- Language: C++20.
- GUI: Qt6 Widgets.
- Async work: `QtConcurrent::run` with `QFutureWatcher` in the UI layer.
- Image backend: libvips/vips-cpp.
- Archive backend: libarchive.
- Persistent preferences: Qt `QSettings` for non-history settings only.
- Tests: Qt Test.
- Build: CMake + Ninja.

The build creates:

- `insidious_core`: static library for core image/viewer logic.
- `insidious`: GUI executable.
- `insidious_core_tests`: Qt Test executable for core behavior.

## Directory structure

- `src/app/main.cpp` initializes libvips, filters known missing-dependency warnings from optional libvips modules outside the MVP format set, sets libvips cache limits, creates `QApplication`, parses an optional image/folder path, and opens `MainWindow`.
- `src/ui/main_window.*` owns the top-level Qt window, actions, shortcuts, single/multi-path open and drag'n'drop classification, statusbar, fullscreen, async scan/decode jobs, cache lookup, and neighbor prefetch.
- The normal main menu is built from the same `QAction` objects as toolbars but is hidden by default; a plain `Alt` key press shows it transiently for keyboard menu navigation, and it hides again after action trigger, popup close, or focus loss. The normal toolbar exposes a right-aligned compact app menu button, while the fullscreen toolbar keeps the same button inside the main button cluster so the right edge overlay does not cover it.
- `src/ui/settings_dialog.*` implements the Preferences dialog. It edits an `AppSettings` value and leaves persistence/schema/defaults to core.
- `src/ui/image_info_panel.*` shows current-entry information in the right `Info` dock. It consumes `ImageEntry`, `DecodedImage::sourceSize`, and EXIF metadata fields collected by the decoder; it does not read files.
- `src/ui/collection_tree_model.*` exposes the current source folder tree to `QTreeView` instances in the normal and fullscreen `Files` panels. It keeps path/node row indexes for large trees and updates lazy-loaded folders with targeted row insertions instead of resetting the whole model.
- `src/ui/image_list_model.*` exposes the active folder's image entries to `QListView` instances in the left `Files` panel. It is UI-only and does not scan sources, decode images, or build thumbnails.
- `src/ui/image_view_widget.*` paints the current decoded `QImage`, shows loading/error messages, and translates mouse events into high-level commands.
- `src/ui/status_line_formatter.*` formats the compact statusbar/fullscreen bottom text and the separate pinned fullscreen info line from current entry, decode metadata, zoom state, and collection index. The status line renders each field as a muted label plus value with vertical separators, keeps file and EXIF dates distinguishable as `modified:` and `taken:`, normalizes dates to `YYYY.M.D HH:MM:SS`, and shows zoom as a percentage. The pinned fullscreen line uses the separate `name (resolution | filesize) [index/count] zoom` field set.
- `src/core/image_entry.*` defines the collection entry metadata used by viewer state and cache keys, including filesystem and archive-backed entries.
- `src/core/image_entry_validation.*` validates that a filesystem image or owning archive is still loadable before decode.
- `src/core/image_formats.*` contains the MVP image extension allowlist: JPEG, PNG, WebP, BMP, DIB, plus archive extension detection for ZIP, 7z, and RAR.
- `src/core/natural_sort.*` contains case-folded natural ordering for folder entries.
- `src/core/folder_collection_source.*` supports the older flat non-recursive folder scan and the current source-tree flow: root folder scan plus lazy per-folder loading. `CollectionFolderNode` also carries optional filesystem lazy-load metadata and a flag for temporary file-list sources that initially preserve input order.
- `src/core/archive_collection_source.*` scans ZIP/7z/RAR archives with libarchive, builds virtual folder trees from archive headers, filters supported image entries, and still provides bounded full-entry reads for features that require original bytes.
- `src/core/multi_collection_source.*` builds synthetic source trees for homogeneous multi-image, multi-folder, and multi-archive open/drop operations. It preserves top-level input order, namespaces folder paths to avoid collisions, keeps multi-folder lazy loading, and reuses archive scans without disk extraction.
- `src/core/archive_entry_stream.*` opens one archive member as a sequential in-memory stream for display decode without extracting it to disk or buffering the full member first.
- `src/core/viewer_controller.*` stores collection position and view state: zoom mode, zoom factor, pan offset, and view-only rotation.
- `src/core/app_settings.*` defines persistent settings defaults, enum/string conversion, validation/clamping, category reset behavior, and `QSettings` read/write.
- `src/core/decoded_image.h` defines decode requests and decode results shared by UI/cache/decoder, including decoded dimensions, region-render metadata, metadata fields, and machine-readable decode error reasons.
- `src/core/memory_image_cache.*` is an in-memory LRU-ish decoded image cache bounded by decoded bytes.
- `src/core/render_memory_budget.*` estimates the memory required for an output raster and exposes the shared render-budget guard. For `fit` this is the full display-sized raster; for non-fit zoom/actual-size this is the decoded visible region plus overscan when region rendering is enabled, or the full scaled raster when it is disabled.
- `src/core/vips_image_decoder.*` uses libvips to decode filesystem images from paths and archive images from sequential archive streams, read a curated set of useful string EXIF metadata, optionally apply EXIF autorotate, scale, convert to sRGB-like 8-bit RGB/RGBA, and return a `QImage` that owns the libvips memory buffer through a cleanup callback.
- `tools/memory_audit.cpp` and `tools/memory_audit.py` provide developer-only total peak memory audit tooling using synthetic temporary fixtures and Linux `VmHWM`.
- `tests/core_tests.cpp` covers natural sort, extension filtering, folder/archive/multi-source scanning, source-tree grouping, viewer navigation, settings read/write validation, cache eviction, filesystem PNG decode, archive PNG decode, and broken file/archive errors.

## Data flow

Opening one image:

1. `MainWindow::openPath()` detects a file and calls `openFile()`.
2. `singleImageCollection()` validates the file and creates a one-entry collection so the selected image can load immediately.
3. `MainWindow` starts an async scan of the parent folder with the selected path.
4. `ViewerController` holds the current entry and view state.
5. `MainWindow::loadCurrentImage()` checks `MemoryImageCache`; on a miss it starts a `QtConcurrent` decode job. Same-entry re-render requests may be stored as the newest pending decode while another render for that entry is active.
6. `VipsImageDecoder::decode()` returns a display raster as `DecodedImage`.
7. `MainWindow` ignores stale decode results via generation counters, updates `ImageViewWidget`, then prefetches previous/next entries only for fit-sized decode requests. Same-entry render reloads are coalesced so only the newest pending request is kept while the active render finishes; switching to another entry starts a new foreground decode immediately. Prefetch jobs are tracked by cache key, coalesced while in flight, and use `MemoryImageCache` generation guards so results decoded before a cache clear cannot repopulate a new source cache.

Transient loading display:

- Cache misses during navigation keep the previous `ImageViewWidget` raster visible and show `Loading image...` as a translucent overlay only after a short UI-thread timer delay, so fast decodes do not flash a loading label.
- Cache misses during zoom keep the previous raster and decoded state visible without showing a loading label and without moving the old raster before the new decode is ready.
- Non-fit zoom and actual-size requests use region-based rendering by default: `DecodeRequest` carries the viewport and pan offset, the decoder renders the visible virtual-image area plus overscan, and `ImageViewWidget` positions that tile inside the virtual full image rectangle. Region tiles are not stored in the shared decoded image cache. `Preferences -> Viewer -> Render only visible zoom region` can disable this and render the full scaled raster for smoother panning at higher memory cost; full-raster non-fit cache keys ignore pan offset because pan does not change the decoded output.
- Scaling/filter quality is a viewer setting carried by `DecodeRequest` and included in decoded cache keys. The decoder maps presets to libvips resize kernels: linear, Mitchell, Lanczos3, or MKS2021. Changing the preset reloads the current image; non-fit reloads preserve the previous view state until the new raster is ready.
- While a stale raster is displayed, image-specific interactions such as pan, click zoom, rectangle zoom, wheel zoom, toolbar zoom, fit/actual size, and rotate are disabled; image navigation and fullscreen remain available.
- If a zoom-triggered decode exceeds the render budget, `VipsImageDecoder` reports `DecodeErrorReason::RenderTooLarge`; `MainWindow` restores the previous zoom/pan/raster and shows a compact `Too large to render` overlay instead of replacing the image with a full error message.
- This budget is calculated by `render_memory_budget` and estimates the final output raster only; total peak memory can also include libvips working/cache memory, source-reader buffering, the displayed raster, and decoded cache contents.
- `docs/memory-audit.md` records the current synthetic peak RSS baseline for filesystem, archive, zoom, and prefetch scenarios.
- Full messages still clear the image for initial loading, scanning, empty states, and current-entry errors.

Current image failures:

- `MainWindow::loadCurrentImage()` validates that the current filesystem entry still exists, is a regular file, and is readable before cache/decode work.
- Missing, unreadable, non-file, and decode-failed entries show an error for the current filename but keep the collection and index.
- The user can navigate away from a failed entry with next/previous actions.
- Failed loads are not inserted into `MemoryImageCache`; prefetch failures stay silent.

Opening a folder:

1. `MainWindow::openFolder()` clears the current collection and starts an async `scanFileSystemSourceTree()`.
2. The root folder scan returns direct images plus direct child folders; descendants are not scanned immediately.
3. Selecting or expanding an unloaded filesystem folder starts async `loadFileSystemFolder()` for that folder.
4. `MainWindow::activateFolder()` installs the selected folder's direct image entries as the current `ViewerController` collection.

Opening an archive:

1. `MainWindow::openPath()` detects ZIP/7z/RAR files and calls `openArchive()`.
2. `openArchive()` clears the current collection and starts async `scanArchiveSourceTree()`.
3. `scanArchiveSourceTree()` uses libarchive to enumerate headers, filter supported image extensions, build virtual folder nodes, and sort folders/files naturally.
4. Archive-backed `ImageEntry` values keep both the archive path and internal entry path; the active folder's file list and quick jump treat direct images in that folder as the current collection.
5. If libarchive reports that a password is required or invalid, `MainWindow` prompts the user and retries the scan until the password works or the user cancels.
6. The current archive password is stored only in `MainWindow` memory for the active archive source and is passed by value to archive scan/decode jobs; it is never written to `ImageEntry`, cache keys, docs, logs, settings, or UI labels.
7. During display decode, `VipsImageDecoder` wraps the selected archive entry in an unseekable sequential `VipsSourceCustom` stream and passes it to `vips::VImage::new_from_source()`. The selected entry is not buffered fully in memory first.

Opening multiple paths:

1. `MainWindow::openPaths()` accepts multiple paths only when they are homogeneous: all supported image files, all folders, or all supported archives.
2. Multiple image files are validated synchronously and installed as a flat temporary source under a synthetic `Selected files` root. The initial order is the input order; explicit Sort then sorts the active list normally.
3. Multiple folders are scanned asynchronously into a synthetic `Selected folders` root. Each selected folder is a top-level node in input order, and child folders keep their real filesystem root/load path so lazy loading still reads the correct directory.
4. Multiple archives are scanned asynchronously into a synthetic `Selected archives` root. Each selected archive is a top-level node in input order, with its own virtual folder tree rebased under a unique synthetic path.
5. Mixed multi-path sets and unsupported paths fail without installing a partial source.
6. Multi-archive password handling prompts once when a password is required, applies that password to the selected archives, stores it only in memory for the current source, and aborts the open if any archive rejects it.

Left file list:

- `MainWindow` owns a left `QDockWidget` named `Files` with a vertical `QSplitter`: `QTreeView` above and `QListView` below.
- The tree uses `CollectionTreeModel` and represents folders for the current filesystem/archive source or the synthetic root for multi-source opens.
- The list uses `ImageListModel` and is reset from `ViewerController::entries()` whenever the active folder changes.
- File sorting is runtime-only state in `MainWindow`: active-folder images are copied, sorted by `core::sortImageEntries()`, and then installed into `ViewerController`. Folder tree order and stored `CollectionFolderNode::images` stay in natural source order. Temporary selected-file collections initially preserve input order until the user explicitly applies Sort.
- Selecting a folder activates only that folder's direct images as the viewer collection; selecting a file row calls `ViewerController::setCurrentIndex()` and then loads through the existing image-switch path.
- Programmatic navigation syncs the selected file row and the selected/expanded folder in both normal and fullscreen panels.
- `next/previous` first navigate inside the active folder collection. At folder boundaries, `MainWindow` uses a cached preorder traversal of the source tree to activate the next/previous loaded folder with images or unloaded filesystem folder; true source boundaries keep the existing reset-and-rerender behavior. The traversal cache is rebuilt only when the source root changes or a lazy-loaded folder adds children.
- Explicit previous/next folder commands use the same adjacent-folder traversal directly and are exposed as configurable shortcuts. Defaults are `MouseBack` for previous folder and `MouseForward` for next folder. Both explicit folder commands select the first image in the destination folder after the current runtime sort is applied; plain previous-image navigation at a folder boundary still opens the previous folder at its last sorted image for continuous reverse browsing.

Quick filename jump:

- `Navigate -> Quick Jump` / `Ctrl+F` opens a UI-only dialog backed by `ImageListModel` and a case-insensitive `QSortFilterProxyModel`.
- Matching is contains-based against the display name only; no filesystem scan or metadata lookup happens in the dialog.
- Matching is scoped to the active folder collection, not the whole source tree.
- In fullscreen, `MainWindow` keeps the viewer fullscreen and shows the same dialog inside an in-window overlay, avoiding a separate top-level dialog that can make the window manager leave fullscreen.
- Accepted selection maps the proxy row back to the collection row and switches through `MainWindow::reloadAfterImageSwitch()`.

Clipboard:

- `Edit -> Copy Original Image` / `Ctrl+C` copies the current source image bytes to the clipboard with the MIME type derived from the original extension and also adds Qt clipboard image data decoded from those same source bytes for paste compatibility.
- The command does not use `DecodedImage` or the rendered raster, so zoom, rotate, fit mode, display scaling, and EXIF autorotate do not affect the clipboard payload.
- Filesystem images are read from `ImageEntry::absolutePath`; archive images are read through `readArchiveEntry()` with the current source's in-memory archive password for that archive path and are not extracted to disk. This clipboard path intentionally reads the full original entry bytes and remains separate from streaming display decode.
- Clipboard reads run on a separate `QFutureWatcher`, are limited to 256 MiB, and stale results are ignored if the current entry changes before the read finishes.
- Clipboard payload stores image bytes and decoded image data only. Archive passwords and filesystem/archive paths are not written into clipboard metadata.

Fullscreen overlay UI:

- Entering fullscreen stores the previous non-fullscreen window state plus normal visibility of the menu bar, main toolbar, statusbar, `Files` dock, and `Info` dock, hides the UI chrome, and makes the central image viewer occupy the window. Exiting fullscreen restores the saved non-fullscreen window state instead of calling `showNormal()`, which keeps tiling/maximized window-manager geometry more predictable.
- Fullscreen edge UI is implemented as child overlay widgets of `MainWindow`, not as dock widgets and not as layout participants. Showing or hiding these overlays must not resize `ImageViewWidget`, alter zoom/pan, or trigger image reload.
- The left overlay owns a second `QTreeView` and `QListView` backed by the same tree/list models as the normal `Files` dock. The views have separate selection models and are synchronized from the active folder and `ViewerController::currentIndex()`.
- The right overlay owns a second `ImageInfoPanel` and receives the same entry/decode/error updates as the normal `Info` dock.
- The top overlay is a `QToolBar` using the same `QAction` objects as the normal menu/toolbar flow, so shortcuts, enabled state, checkable state, and command routing remain centralized.
- The bottom overlay mirrors the normal statusbar text. Both use `status_line_formatter` and currently share the same labelled field set: decoded dimensions/MP, file size, file modified date, EXIF date, display path, zoom percentage, and index/count; in `fit` mode the zoom field shows the actual displayed scale rather than the literal word `fit`.
- A separate pinned fullscreen info line is a child `QLabel` over the image area, not part of the edge panel set and not hidden by `hideFullscreenOverlayPanels()`. It is enabled by default, anchored at the top-left of the fullscreen window, uses `name (resolution | filesize) [index/count] zoom`, and may be visually covered by edge panels.
- Hover detection uses settings-backed `FullscreenOverlaySettings`: side triggers are 7% of window width by default, top/bottom triggers are 5% of window height by default. Side overlays have priority in corners.
- `Ctrl+F` quick jump suppresses fullscreen edge overlays while its in-window modal overlay is open.
- Exiting fullscreen hides all edge overlays, returns to normal window mode, and restores the stored normal UI visibility.

Right info panel:

- `MainWindow` owns a right `QDockWidget` named `Info`, hidden by default and toggled from the `View` menu.
- The panel shows `ImageEntry` metadata immediately when the current entry changes.
- The widget groups rows into compact `Info` and `EXIF` sections; empty values are rendered as `N/A`.
- Decoded dimensions are filled from `DecodedImage::sourceSize` after successful decode.
- EXIF is displayed from `DecodedImage::metadataFields`; only a compact set of useful libvips `exif-*` string fields is collected during decode.
- Current-entry errors keep entry metadata visible and mark dimensions unavailable.

Settings:

- `Edit -> Preferences` opens `SettingsDialog`.
- Settings are loaded in `MainWindow` construction and saved on Apply/OK and window close through `QSettings` using the application/organization name `Insidious`.
- `SettingsDialog` keeps the persistence schema in `AppSettings` and presents existing controls in sectioned pages: viewer defaults/zoom/wheel actions, shortcut assignments, fullscreen edge/info-line settings, and interface appearance/layout.
- Viewer settings currently include default zoom mode, image-switch view policy, left-button zoom behavior, EXIF autorotate, click zoom multiplier, shared zoom step, scaling quality, region rendering, and wheel / Ctrl+wheel / Shift+wheel actions. EXIF autorotate is also exposed as a synced checkable action in the `View` menu, main toolbar, and fullscreen top overlay.
- Shortcut settings currently include core viewer commands only. `AppSettings` owns stable command ids, keyboard/mouse input strings, defaults, QSettings read/write, reset behavior, and invalid-input fallback. `MainWindow` maps keyboard inputs onto existing `QAction` shortcuts and triggers the same actions for matching mouse button inputs, so menus, the normal toolbar, and the fullscreen top overlay share command routing. `CopyOriginalImage` defaults to `Ctrl+C`.
- `SettingsDialog` exposes a `Shortcuts` page where a command can have keyboard and mouse inputs at the same time, be cleared, or reset to defaults. `Set Keyboard` replaces only keyboard inputs for that command, while `Set Mouse` replaces only mouse inputs. Apply/OK rejects duplicate non-empty inputs across the editable command set.
- Fullscreen settings currently include side and top/bottom edge trigger ratios plus pinned info line enabled/font family/font size/color.
- Interface settings currently include normal-mode light/dark theme, whether to remember layout, window geometry/state, and normal/fullscreen Files splitter state. Fullscreen overlay chrome remains dark-only.
- Settings must stay privacy-safe: do not store recent files, source paths, archive paths, default image folders, archive passwords, thumbnails, decoded image data, or file logs. Shortcut settings store only command ids and portable keyboard/mouse input strings.
- `AppSettings::read()` clamps numeric values and falls back on defaults for unknown enum strings so older or manually edited config files remain safe.

Navigation:

1. Actions, shortcuts, wheel events, or mouse gestures call `ViewerController` commands through `MainWindow`.
2. Next/previous image switching first tries `ViewerController` inside the active folder, then activates the next/previous suitable folder in source-tree preorder when the folder boundary is reached.
3. The current default `ImageSwitchPolicy` is `ResetToBaseMode`, which resets temporary zoom state, pan, and zoom to the selected base mode (`FitToWindow` or `ActualSize`) before loading.
4. If next/previous cannot change the index or folder, `MainWindow` still reloads the current entry after applying the switch policy. This clears transient render errors, including errors caused by an excessive custom zoom request.
5. `ImageSwitchPolicy::PreserveView` keeps the current zoom/pan instead of resetting them and is exposed in Preferences. View rotation is always reset when the current image changes.
6. On a changed index or view transform, `MainWindow` builds a new `DecodeRequest`.
7. Cache keys include file identity and render request details, so fit/zoom/rotation variants do not collide.

Mouse input:

- Wheel navigates previous/next image.
- `ImageViewWidget` emits wheel delta, modifiers, and cursor position to `MainWindow`; `MainWindow` maps them through settings to navigate, zoom, or no action. Defaults are wheel=navigate, `Ctrl+wheel`=zoom, `Shift+wheel`=no action.
- Wheel zoom, toolbar zoom, and shortcut zoom use the same configurable step percent. Wheel zoom anchors on the cursor position; toolbar and shortcut zoom anchor on the viewport center. Zooming from `FitToWindow` starts from the current decoded `effectiveScale`, not from 100%.
- In the default left-button mode, simple left click calls `MainWindow::handleImageClick()`: it enters temporary 2x zoom relative to the current decoded `effectiveScale`, centered on the clicked point, or restores the saved `fit`/`actual size` base mode after click/rectangle zoom.
- `MainWindow::updateInteractionState()` enables the idle magnifier cursor for left-button zoom modes only while the action is actually available: a decoded image is shown, no render is pending, and hold zoom is not active.
- In the default left-button mode, left-button drag pans the current raster and stores the pan offset in `ViewerController` only outside base `FitToWindow`.
- When the base mode is `FitToWindow` and the current decoded image is already in an enlarged custom view, `ImageViewWidget` enables a fit hand-pan gesture on middle-button hold and `Ctrl+left-button` hold. The gesture only pans the existing enlarged view through the same `panChanged` path; it does not start zoom by itself.
- For region-based decoded images, pan keeps moving the current tile immediately and starts a debounced 120 ms region reload so the visible area catches up without launching a decode on every mouse move.
- In `HoldToZoomPan` mode, `ImageViewWidget` emits hold begin/move/end events; `MainWindow` saves the current `ViewRenderState`, starts temporary zoom with the same multiplier, pans the zoomed raster on mouse movement, and restores the saved view on release. Releasing before decode completion invalidates the pending decode generation and restores immediately.
- Right-button drag draws a rectangle in `ImageViewWidget`; `MainWindow::zoomToRect()` converts that widget rectangle into a custom zoom factor and pan offset.
- Left double click toggles fullscreen. `ImageViewWidget` delays single-click zoom emission by the system double-click interval and cancels it when a double click arrives, so fullscreen toggle does not also trigger temporary zoom.

## Threading and stale results

- UI widgets must only be touched on the UI thread.
- Folder scans, archive scans, and libvips decoding run through `QtConcurrent::run`.
- `MainWindow` uses `m_sourceGeneration` and `m_decodeGeneration` to ignore old async results after the user opens another source or starts another decode.
- Lazy folder load results update only the affected `CollectionTreeModel` branch with `rowsInserted`; opening a new source still resets the full tree model because the source identity changes.
- Main image decode requests are routed through `MainWindow`: repeated same-entry reloads store only the newest pending request and start it after the active watcher finishes, but image switches start a new foreground decode immediately so navigation stays responsive. Cache hits still display immediately and invalidate older active decode results through `m_decodeGeneration`.
- Current prefetch jobs are tracked by `MainWindow`, coalesced by cache key, and only write successful decoded images into the shared memory cache when the cache generation still matches the generation captured at prefetch start. Speculative prefetch is limited to fit-sized decode requests; actual-size and custom zoom requests do not start neighbor prefetch. Archive entries with an in-memory password for the current source skip speculative prefetch so archive passwords are not retained by background neighbor jobs.

## Privacy boundaries

The MVP intentionally avoids:

- Recent file lists.
- Persistent thumbnail caches.
- Persistent decoded image caches.
- File logs in normal usage.
- Archive extraction.
- Archive password persistence.

The app uses `QSettings` only for non-history preferences. Layout persistence stores Qt geometry/state byte arrays and splitter positions; it must not store opened source paths.

File dialogs currently use `QFileDialog::DontUseNativeDialog` to avoid implicit desktop recent-file integration until there is an explicit decision to change this.

## Out of scope for current MVP

- Archive password persistence.
- Recursive "include all descendants" folder collections.
- GIF animation.
- TIFF, AVIF, JPEG XL, RAW.
- Advanced metadata backends and metadata UI beyond libvips-provided EXIF strings.
- Thumbnail panel.
- Configurable or fuzzy quick filename jump.
- Advanced shortcut features such as editing every QAction, profiles/import-export, and configurable shortcut scopes.
- Advanced settings for color management, statusbar content, quick jump behavior, and thumbnail generation.

## Change guidance

- Put widget-specific code in `src/ui`, not `src/core`.
- Keep source scanning, sorting, cache, and decode behavior testable without constructing UI widgets.
- Add or update `tests/core_tests.cpp` for non-trivial core behavior.
- Update `docs/decisions.md` when changing stack, privacy behavior, cache policy, source abstraction, or MVP boundaries.
- Update `docs/roadmap.md` when completing a backlog item, reprioritizing work, or adding/removing a milestone.
- Run `cmake --build build` and `ctest --test-dir build --output-on-failure` after code changes when dependencies are available.
