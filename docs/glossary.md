# Glossary

## Active folder

The selected node in the current source tree. Its direct image entries form the active viewer collection used by image navigation, the list and thumbnail views, Quick Jump, and status indexes.

## Actual size

A view in which one source image pixel maps to one physical display pixel. Widget geometry and input remain in Qt logical pixels.

## Collection

An ordered set of image entries with a current position and navigation operations.

## Collection source

An origin that exposes images to the viewer through collection semantics. Filesystem folders, selected file lists, archives, and homogeneous multi-source selections are represented this way.

## Compare session

A temporary, process-local ordered set of images shown in one comparison mode. The session and its decoded rasters are never persisted.

## Decode request

The complete description of a raster needed for display, including the entry, output geometry, zoom or region, orientation, rotation, scaling quality, color-management mode, and device pixel ratio.

## Display raster

The decoded and transformed `QImage` used for painting. It may be display-sized or region-sized and is distinct from the original source bytes.

## Fit to window

A base view that scales the image to fit inside the viewport while preserving aspect ratio.

## Image entry

One image in a collection. It carries display metadata and the source-specific identity needed to read filesystem or archive-backed content.

## Natural sort

Filename ordering that treats digit runs numerically, so `2.jpg` sorts before `10.jpg`.

## No traces

The privacy contract that forbids persistent viewing history, image caches, archive extraction, saved archive passwords, comparison sessions, and normal-use file logs. Privacy-safe application preferences are allowed.

## Pan

Movement of the displayed image within the viewport without changing its scale.

## Source tree

The folder hierarchy exposed by the current collection source. Filesystem children load lazily; archive folders are built from headers; multi-source selections use a synthetic root.

## View-only transform

A display operation such as rotation that changes the rendered raster without modifying the original file or copied original bytes.
