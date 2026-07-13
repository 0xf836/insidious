# Product Principles

## Purpose

Insidious is a native Linux application for quickly viewing images from files, folders, selected file sets, and archives.

It should feel lightweight, immediate, and predictable. It is an image viewer rather than an editor, media catalogue, or file-history system.

## Primary workflows

### Open one image

Opening an image also uses its parent folder as browsing context, allowing immediate navigation to neighboring supported images.

### Browse a folder tree

Opening a folder presents its direct images and a lazily loaded source tree. Selecting a folder makes its direct images the active viewer collection; image navigation can cross into adjacent suitable folders at collection boundaries.

### Open a temporary selection

Multiple selected images form a flat, process-local collection. Multiple folders or multiple archives form a synthetic source tree in input order. Mixed source types are rejected as one operation rather than partially opened.

### Browse an archive

ZIP, 7z, and RAR archives behave like virtual folder trees. Image data is read without persistent extraction. Archive passwords are requested when required and remain only in memory for the active source.

### Compare images

Selected images can be collected into a temporary comparison session across folders in the current source. Comparison state and decoded comparison rasters remain memory-only.

## Product values

- **Speed:** opening sources, navigation, and ordinary interaction should feel immediate.
- **Predictability:** navigation and failure behavior should not silently change the collection or skip content.
- **Rendering quality:** scaling, HiDPI semantics, orientation, alpha handling, and color conversion should be explicit and testable.
- **Responsiveness:** scanning, archive access, image decoding, metadata extraction, thumbnail generation, and comparison decoding must not block the UI thread.
- **Memory discipline:** display-sized and region-sized rendering, bounded caches, and guarded prefetch are preferred over loading entire collections.
- **Privacy:** normal viewing must not create history, persistent image caches, extracted archive contents, saved passwords, or file logs.

## Deliberate non-goals

Insidious is not currently intended to be:

- an image editor or batch processor;
- a digital asset management system or persistent collection catalogue;
- a file organizer with ratings, tags, or recent-item history;
- an animated-image player;
- a RAW development pipeline;
- a general-purpose archive manager.

Potential formats and advanced features are evaluated as separate product decisions rather than being enabled simply because a dependency can decode them.
