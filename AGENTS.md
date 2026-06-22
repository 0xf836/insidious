# AGENTS.md

## Project

This is a native Linux GUI image viewer. The application should quickly open and browse image files, folders, and later archives containing images. The project prioritizes performance, predictable navigation, rendering quality, and privacy: no activity history, no persistent thumbnail cache, no persistent archive extraction, and no file logs in normal usage.

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

The current MVP implementation is C++20 with Qt6 Widgets and libvips/vips-cpp.

- `CMakeLists.txt` defines the build, `insidious_core`, the `insidious` GUI executable, and `insidious_core_tests`.
- `src/app/main.cpp` owns process bootstrap: initializes libvips, configures libvips cache limits, creates `QApplication`, parses an optional path argument, and shows the main window.
- `src/ui/` is the Qt UI layer. Keep direct widget, action, shortcut, drag'n'drop, statusbar, fullscreen, and paint/event handling here.
- `src/ui/main_window.*` coordinates UI commands, async folder scans, async image decode jobs, stale-result generation counters, cache lookup, and neighbor prefetch.
- `src/ui/image_view_widget.*` paints the current `QImage` and translates mouse input into viewer commands. It should not scan folders or decode images.
- `src/core/` is non-widget logic: image entries, supported format filtering, natural sorting, folder scanning, viewer state, decode request/result types, memory cache, and libvips decoding.
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

## MVP boundaries

The first implementation should focus on regular filesystem images and folders. Archive support, RAW support, advanced metadata, configurable hotkeys, and thumbnail panels are later milestones unless explicitly requested.

Current MVP supports only regular filesystem images and non-recursive folders. The enabled extension filter is JPEG, PNG, WebP, BMP, and DIB. GIF, TIFF, AVIF, JPEG XL, RAW, archives, thumbnails, EXIF side panels, file-list panels, settings UI, and recent-file history are intentionally out of scope.

Current MVP mouse behavior:

- Wheel navigates previous/next image.
- `Ctrl+wheel` zooms in/out.
- Next/previous actions reset zoom and pan to the current base mode (`fit to window` or `actual size`) by default, then reload the current entry. Keep this behavior configurable in code as reset-vs-preserve when image-switch settings are added.
- Simple left click enters temporary default zoom at 200%, centered on the clicked image point.
- Simple left click while in temporary click/rectangle zoom returns to the saved base mode: fit or actual size.
- Hold left mouse button and drag to pan the image only outside base `fit to window`.
- Hold right mouse button and drag a rectangle to zoom to that selected area.
- Double click toggles fullscreen and must not also trigger temporary click zoom.

## Implementation constraints

- Keep `src/core` free of `QWidget`/GUI dependencies. QtCore/QtGui value types such as `QString`, `QImage`, `QSize`, and `QDateTime` are currently accepted there.
- Do not run folder scanning or libvips decoding on the UI thread.
- Guard async UI updates against stale results when the current source or decode request changes.
- Keep cache state memory-only. Do not introduce persistent cache files, recent-file lists, or viewed-path settings.
- Failed current entries must not destroy collection/navigation state. Show a clear error and keep next/previous navigation available.
- Prefer display-sized rasters for fit/zoom rendering instead of forcing full-size decoded images into memory.
- If adding a source type later, make it look like a collection of `ImageEntry` objects to the viewer/controller rather than special-casing it throughout UI code.
- If adding dependencies, update `README.md`, `docs/decisions.md`, and this file with why they are needed.

## Definition of done

A task is done when:

- The requested behavior is implemented and scoped to the task.
- The app still builds/runs.
- Relevant tests are added or updated where practical.
- Error handling is present for common failure modes.
- The final response explains what changed, how it was tested, and what was intentionally left out.
