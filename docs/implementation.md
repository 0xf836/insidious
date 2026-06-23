# Current implementation map

This document describes the current codebase shape for future coding agents. Product requirements remain in the other `docs/` files; this file is about the implementation that exists now.

## Stack

- Language: C++20.
- GUI: Qt6 Widgets.
- Async work: `QtConcurrent::run` with `QFutureWatcher` in the UI layer.
- Image backend: libvips/vips-cpp.
- Tests: Qt Test.
- Build: CMake + Ninja.

The build creates:

- `insidious_core`: static library for core image/viewer logic.
- `insidious`: GUI executable.
- `insidious_core_tests`: Qt Test executable for core behavior.

## Directory structure

- `src/app/main.cpp` initializes libvips, sets libvips cache limits, creates `QApplication`, parses an optional image/folder path, and opens `MainWindow`.
- `src/ui/main_window.*` owns the top-level Qt window, actions, shortcuts, drag'n'drop, statusbar, fullscreen, async scan/decode jobs, cache lookup, and neighbor prefetch.
- `src/ui/image_info_panel.*` shows basic current-entry information in the right `Info` dock. It consumes `ImageEntry` and `DecodedImage::sourceSize` only; it does not parse EXIF or read files.
- `src/ui/image_list_model.*` exposes the current collection to a `QListView` in the left `Files` dock. It is UI-only and does not scan sources, decode images, or build thumbnails.
- `src/ui/image_view_widget.*` paints the current decoded `QImage`, shows loading/error messages, and translates mouse events into high-level commands.
- `src/core/image_entry.*` defines the collection entry metadata used by viewer state and cache keys.
- `src/core/image_entry_validation.*` validates that a filesystem entry is still loadable before decode.
- `src/core/image_formats.*` contains the MVP extension allowlist: JPEG, PNG, WebP, BMP, DIB.
- `src/core/natural_sort.*` contains case-folded natural ordering for folder entries.
- `src/core/folder_collection_source.*` scans non-recursive filesystem folders and creates `ImageEntry` vectors.
- `src/core/viewer_controller.*` stores collection position and view state: zoom mode, zoom factor, pan offset, and view-only rotation.
- `src/core/decoded_image.h` defines decode requests and decode results shared by UI/cache/decoder.
- `src/core/memory_image_cache.*` is an in-memory LRU-ish decoded image cache bounded by decoded bytes.
- `src/core/vips_image_decoder.*` uses libvips to autorotate, scale, convert to sRGB-like 8-bit RGB/RGBA, and return a copied `QImage`.
- `tests/core_tests.cpp` covers natural sort, extension filtering, folder scanning, viewer navigation, cache eviction, and a libvips PNG decode smoke test.

## Data flow

Opening one image:

1. `MainWindow::openPath()` detects a file and calls `openFile()`.
2. `singleImageCollection()` validates the file and creates a one-entry collection so the selected image can load immediately.
3. `MainWindow` starts an async scan of the parent folder with the selected path.
4. `ViewerController` holds the current entry and view state.
5. `MainWindow::loadCurrentImage()` checks `MemoryImageCache`; on a miss it starts a `QtConcurrent` decode job.
6. `VipsImageDecoder::decode()` returns a display raster as `DecodedImage`.
7. `MainWindow` ignores stale decode results via generation counters, updates `ImageViewWidget`, then prefetches previous/next entries.

Transient loading display:

- Cache misses during navigation keep the previous `ImageViewWidget` raster visible and show `Loading image...` as a translucent overlay.
- Cache misses during zoom keep the previous raster visible without showing a loading label and without moving the old raster before the new decode is ready.
- While a stale raster is displayed, image-specific interactions such as pan, click zoom, and rectangle zoom are disabled; wheel navigation and fullscreen remain available.
- Full messages still clear the image for initial loading, scanning, empty states, and current-entry errors.

Current image failures:

- `MainWindow::loadCurrentImage()` validates that the current filesystem entry still exists, is a regular file, and is readable before cache/decode work.
- Missing, unreadable, non-file, and decode-failed entries show an error for the current filename but keep the collection and index.
- The user can navigate away from a failed entry with next/previous actions.
- Failed loads are not inserted into `MemoryImageCache`; prefetch failures stay silent.

Opening a folder:

1. `MainWindow::openFolder()` clears the current collection and starts an async `scanFolder()`.
2. `scanFolder()` returns naturally sorted, non-recursive supported image entries.
3. `MainWindow::applyScanResult()` installs the collection, refreshes the left file list, and loads the selected image.

Left file list:

- `MainWindow` owns a left `QDockWidget` named `Files` with a `QListView` backed by `ImageListModel`.
- The model is reset from `ViewerController::entries()` whenever the collection changes.
- Programmatic navigation syncs the selected row from `ViewerController::currentIndex()`.
- Selecting a row by mouse or keyboard calls `ViewerController::setCurrentIndex()` and then loads through the existing image-switch path.

Right info panel:

- `MainWindow` owns a right `QDockWidget` named `Info`, hidden by default and toggled from the `View` menu.
- The panel shows `ImageEntry` metadata immediately when the current entry changes.
- Decoded dimensions are filled from `DecodedImage::sourceSize` after successful decode.
- Current-entry errors keep entry metadata visible and mark dimensions unavailable.

Navigation:

1. Actions, shortcuts, wheel events, or mouse gestures call `ViewerController` commands through `MainWindow`.
2. Next/previous image switching goes through `MainWindow::reloadAfterImageSwitch()`. The current default `ImageSwitchViewPolicy` is `ResetToBaseMode`, which resets temporary zoom state, pan, and zoom to the selected base mode (`FitToWindow` or `ActualSize`) before loading.
3. If next/previous cannot change the index, `MainWindow` still reloads the current entry after applying the switch policy. This clears transient render errors, including errors caused by an excessive custom zoom request.
4. `ImageSwitchViewPolicy::PreserveView` is reserved for a future setting where image switches keep the current zoom/pan instead of resetting them.
5. On a changed index or view transform, `MainWindow` builds a new `DecodeRequest`.
6. Cache keys include file identity and render request details, so fit/zoom/rotation variants do not collide.

Mouse input:

- Wheel navigates previous/next image.
- `Ctrl+wheel` zooms in/out relative to the current decoded `effectiveScale`, so zooming from `FitToWindow` starts from the actual fit scale rather than from 100%.
- Simple left click calls `MainWindow::handleImageClick()`: it enters temporary 2x zoom relative to the current decoded `effectiveScale`, centered on the clicked point, or restores the saved `fit`/`actual size` base mode after click/rectangle zoom.
- Left-button drag pans the current raster and stores the pan offset in `ViewerController` only outside base `FitToWindow`.
- Right-button drag draws a rectangle in `ImageViewWidget`; `MainWindow::zoomToRect()` converts that widget rectangle into a custom zoom factor and pan offset.
- Left double click toggles fullscreen. `ImageViewWidget` delays single-click zoom emission by the system double-click interval and cancels it when a double click arrives, so fullscreen toggle does not also trigger temporary zoom.

## Threading and stale results

- UI widgets must only be touched on the UI thread.
- Folder scans and libvips decoding run through `QtConcurrent::run`.
- `MainWindow` uses `m_sourceGeneration` and `m_decodeGeneration` to ignore old async results after the user opens another source or starts another decode.
- Current prefetch jobs are fire-and-forget and only write successful decoded images into the shared memory cache.

## Privacy boundaries

The MVP intentionally avoids:

- `QSettings`.
- Recent file lists.
- Persistent thumbnail caches.
- Persistent decoded image caches.
- File logs in normal usage.
- Archive extraction.

File dialogs currently use `QFileDialog::DontUseNativeDialog` to avoid implicit desktop recent-file integration until there is an explicit decision to change this.

## Out of scope for current MVP

- Archives and archive passwords.
- Recursive folders.
- Multiple selected files as a temporary collection.
- GIF animation.
- TIFF, AVIF, JPEG XL, RAW.
- EXIF reader and advanced metadata UI.
- Thumbnail panel.
- Quick filename jump.
- Settings UI and configurable hotkeys.

## Change guidance

- Put widget-specific code in `src/ui`, not `src/core`.
- Keep source scanning, sorting, cache, and decode behavior testable without constructing UI widgets.
- Add or update `tests/core_tests.cpp` for non-trivial core behavior.
- Update `docs/decisions.md` when changing stack, privacy behavior, cache policy, source abstraction, or MVP boundaries.
- Run `cmake --build build` and `ctest --test-dir build --output-on-failure` after code changes when dependencies are available.
