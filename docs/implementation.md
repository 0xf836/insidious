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
- `src/ui/image_view_widget.*` paints the current decoded `QImage`, shows loading/error messages, and translates mouse events into high-level commands.
- `src/core/image_entry.*` defines the collection entry metadata used by viewer state and cache keys.
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

Opening a folder:

1. `MainWindow::openFolder()` clears the current collection and starts an async `scanFolder()`.
2. `scanFolder()` returns naturally sorted, non-recursive supported image entries.
3. `MainWindow::applyScanResult()` installs the collection and loads the selected image.

Navigation:

1. Actions, shortcuts, wheel events, or mouse gestures call `ViewerController` commands through `MainWindow`.
2. On a changed index or view transform, `MainWindow` builds a new `DecodeRequest`.
3. Cache keys include file identity and render request details, so fit/zoom/rotation variants do not collide.

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
- EXIF side panel and advanced metadata UI.
- Thumbnail panel.
- File list panel and quick filename jump.
- Settings UI and configurable hotkeys.

## Change guidance

- Put widget-specific code in `src/ui`, not `src/core`.
- Keep source scanning, sorting, cache, and decode behavior testable without constructing UI widgets.
- Add or update `tests/core_tests.cpp` for non-trivial core behavior.
- Update `docs/decisions.md` when changing stack, privacy behavior, cache policy, source abstraction, or MVP boundaries.
- Run `cmake --build build` and `ctest --test-dir build --output-on-failure` after code changes when dependencies are available.
