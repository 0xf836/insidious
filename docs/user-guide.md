# User Guide

## Opening content

Insidious can open an image, a folder, an archive, or a homogeneous selection of several sources.

- Use **File → Open File** to select one or more images or archives.
- Use **File → Open Folder** to select a folder.
- Drop images, folders, or archives onto the window.
- Pass an optional path when launching `insidious` from a terminal.

Opening a single image also scans its parent folder and selects that image, so previous and next navigation can continue through neighboring supported files.

Several images become one temporary flat collection. Several folders or several archives appear under a synthetic root in the order selected. A mixed set such as an image plus an archive is rejected as one operation; Insidious never opens only the supported part of a drop.

Supported images are JPEG, PNG, WebP, BMP, and DIB. Supported archives are ZIP, 7z, and RAR.

## Main window

The main window contains:

- a central **Viewer** and **Compare** tab area;
- a top toolbar for common source, navigation, file, and view commands;
- a left **Files** panel with a source tree and List/Thumbnails tabs;
- an optional right **Info** panel;
- a compact status line.

The traditional menu bar is hidden by default. Press and release `Alt` to use it from the keyboard, or open the compact application menu at the right side of the toolbar. The toolbar can be restored through **View → Toolbar** if it is hidden.

Viewer commands in menus, toolbars, fullscreen controls, and shortcuts share the same actions, so enabled and checked state stays synchronized.

## Browsing folders and archives

The upper half of the Files panel shows folders in the current source. Selecting a folder makes its direct images the active viewer collection and updates both List and Thumbnails.

Filesystem folders load lazily when selected or expanded. Archive folders are built from archive headers and behave like virtual folders; image entries are read only when needed and are not extracted to disk.

The lower List tab shows images in the active folder. Selecting a row displays that image. `Ctrl` and `Shift` selection can build a set for comparison without changing how ordinary single selection opens an image.

Previous and next image commands move inside the active folder, then continue into the previous or next suitable folder in tree order when a boundary is reached. First and last stay inside the active folder. `MouseBack` and `MouseForward` explicitly move to the previous or next suitable folder and select its first image under the current sort.

## Sorting files

Use the toolbar **Sort** menu to order the active-folder image list by:

- natural name;
- type;
- size;
- date.

Each key supports ascending and descending order. Sorting affects image lists only, not folder order or top-level multi-source order.

The initial mode for every process is natural name ascending. A changed mode applies to subsequently opened folders and sources during that process but is not saved in Preferences. A temporary collection of selected images initially keeps selection order until Sort is used.

## Viewing images

### Fit and actual size

**Fit to Window** scales the image to remain inside the viewport. **Actual Size** maps one source pixel to one physical display pixel, including on HiDPI screens.

Preferences selects which mode is the base view used on open and after navigation resets. The status line shows the effective displayed scale as a percentage, including in fit mode.

### Zoom and pan

The default zoom step is 25% and is shared by wheel, toolbar, and shortcut zoom. `Ctrl+wheel` zooms around the pointer. Toolbar and shortcut zoom use the viewport center.

By default, a simple left click temporarily zooms relative to the currently displayed scale and centers the clicked image point. A later simple click after click zoom or rectangle zoom restores the fit or actual-size base view. The multiplier is configurable and defaults to 2×.

Hold the left button and drag to pan when the image is already outside the base fit view. In any non-fit view whose image is larger than the viewport, middle-button drag or `Ctrl+left` drag also pans without starting zoom.

Hold and drag the right button to draw a zoom rectangle. A later simple left click restores the base view.

Preferences can replace click-to-zoom with **Hold to Zoom, Drag to Pan**. In that mode, pressing the left button enters temporary zoom immediately, dragging pans the zoomed image, and release restores the exact previous view.

The magnifier cursor appears only when the current left-button zoom or restore action can run. It is hidden while an image is loading, a render is pending, or hold zoom is active.

### Rendering options

Preferences provides four scaling presets:

- **Fast (linear)**;
- **Balanced (Mitchell)**;
- **High quality (Lanczos3)**, the default;
- **Sharpest (MKS2021)**.

Embedded ICC conversion is enabled by default. Profiled images are converted to sRGB when supported; unprofiled images are treated as sRGB. A broken profile or unavailable conversion support falls back without making the image unreadable. Monitor-profile matching is not currently provided.

Non-fit views render the visible image region plus an overscan margin by default to control memory use. Disable region rendering in Preferences for smoother long pans at the cost of potentially much larger rasters and earlier render-budget failures.

### Rotation and EXIF orientation

**View → Rotate Left** and **Rotate Right** apply a view-only 90-degree transform. Rotation changes neither the original file nor **Copy Original Image**, and resets when the current image changes.

EXIF orientation is enabled by default and can be toggled in Preferences or through the synchronized **Use EXIF Orientation** action. It affects display decode, not original bytes.

## Navigation and Quick Jump

By default, changing images resets zoom and pan to the selected fit or actual-size base mode. Preferences can preserve the current view instead. Rotation still resets on every image change.

Use **Navigate → Quick Jump** or `Ctrl+F` to search filenames in the active folder. Matching is case-insensitive and contains-based. It does not search other source-tree folders, file contents, or metadata.

Quick Jump remains inside the application window in fullscreen so opening it does not force a window-manager fullscreen transition.

## Thumbnails

Open the **Thumbnails** tab in the Files panel to request previews for the active folder. Only visible and nearby thumbnails are generated. Work starts only while the tab and panel are visible.

Closing or hiding the tab, changing sources, or leaving the thumbnail context stops or invalidates pending work. Thumbnails are held in a bounded memory cache and disappear when the process exits.

Thumbnail selection follows the same viewer and comparison behavior as the List tab.

## Comparing images

Select images in List or Thumbnails, open the context menu, and choose a command from **Compare**:

- **Add to Compare** appends new images to the current session without opening the tab.
- **Open Compare** displays the accumulated session.
- **In Row**, **Grid**, or **Carousel** replaces the session with the current selection.
- **Hover Switch** replaces the session with exactly two selected images.

You can build a session from different folders in the same source: add one selection, move to another folder, and add more. Selected List or Thumbnail items can also be dragged onto an already visible Compare tab. Only internal items from the current source are accepted.

Row, Grid, and Carousel support up to eight images. Hover Switch requires exactly two. An append that would exceed the limit or violate the current mode is rejected without changing the session.

### Row

Images share the available width in horizontal tiles. Each tile has independent zoom and pan. Drag tiles to reorder them, use the context menu to remove one, or select a tile and press `Delete`.

### Grid

Images use an automatically chosen grid that fits the current panel. Each cell has independent zoom and pan and supports the same reorder and removal operations as Row.

### Carousel

One image is visible at a time. Wheel, `Left`/`Right`, and `PageUp`/`PageDown` move cyclically through the session. `Ctrl+wheel` zooms, left drag pans an enlarged slide, and `Delete` or the context menu removes the current slide.

### Hover Switch

The first image is shown while the pointer is outside the visible image area; moving over the image reveals the second. Both images use one viewport and shared zoom and pan. Moving the pointer into unused viewport space returns to the first image.

### Alignment

The comparison header offers Fit Center, Top Left, Top Center, Center Left, Bottom Left, Bottom Center, Same Source Scale, and Cover Center. Alignment is runtime-only and changing it resets comparison zoom and pan.

Comparison sessions, order, alignment, decoded rasters, and cache contents stay only in memory. They survive active-folder changes inside the current source, but a new source clears them.

While Compare is the active tab, single-image commands such as navigation, rotate, Quick Jump, and original-image copy are disabled so hidden viewer state cannot change accidentally.

## Fullscreen

Press `F11` or left double click to toggle fullscreen. Press `Esc` to leave it.

### Viewer fullscreen

Viewer fullscreen initially shows only the image plus the optional pinned information line. Move the pointer near an edge to reveal overlays:

- left: Files tree and List/Thumbnails;
- right: Info;
- top: common actions and the compact application menu;
- bottom: the status line.

Side overlays take priority in corners. Their default trigger widths are 7% of the window; top and bottom triggers use 5% of the window height. These values and the pinned line’s visibility, font, size, and color are configurable.

Overlays float above the image. Showing or hiding them does not change the viewport, zoom, pan, or current decode.

### Compare fullscreen

Compare fullscreen shows only the active comparison layout. It keeps the current mode, alignment, order, zoom, and pan and does not use viewer edge overlays. Leave it with `Esc`, `F11`, or left double click on the comparison layout.

## Information and clipboard

Enable **View → Info** to show the right panel. It contains compact Info and EXIF sections with the current entry’s path, type, size, modified time, decoded dimensions, color profile state, and selected EXIF values supplied by libvips. Missing values display as unavailable.

Use **Edit → Copy Original Image** or `Ctrl+C` to copy the source bytes. The clipboard payload is not affected by zoom, fit, rotation, EXIF orientation, scaling quality, or color conversion. Archive entries are read into a bounded memory buffer without disk extraction.

## Preferences

**Edit → Preferences** groups settings into Viewer, Shortcuts, Fullscreen, and Interface pages.

Viewer settings cover the base view, image-switch policy, left-button behavior, EXIF orientation, click multiplier, wheel mappings, zoom step, scaling quality, color conversion, and region rendering.

Shortcuts can assign keyboard sequences and mouse buttons to core viewer commands. A command may have both, may be cleared, and may be restored to its default. Duplicate non-empty inputs must be resolved before Apply or OK. Quit and dock toggles are not part of the current editor.

Fullscreen settings cover edge trigger zones and the pinned information line. Interface settings cover the normal light or dark theme and optional window-layout persistence. Fullscreen chrome remains dark in both themes.

Settings contain preferences and Qt layout state only. They never contain opened paths, archive passwords, thumbnails, decoded images, or comparison sessions.

## Default controls

| Action | Default input |
| --- | --- |
| Open file | `Ctrl+O` |
| Open folder | `Ctrl+Shift+O` |
| Previous image | `Left`, `PageUp` |
| Next image | `Right`, `PageDown` |
| Previous folder | `MouseBack` |
| Next folder | `MouseForward` |
| First image | `Home` |
| Last image | `End` |
| Quick Jump | `Ctrl+F` |
| Copy Original Image | `Ctrl+C` |
| Fit to Window | `0` |
| Actual Size | `1` |
| Rotate left / right | `[` / `]` |
| Fullscreen | `F11` |
| Leave fullscreen | `Esc` |

Zoom In, Zoom Out, Preferences, and Quit use the platform’s standard Qt shortcuts where available.

## Loading and errors

When navigation requires a decode, the previous image remains visible. A subtle loading overlay appears only if loading lasts long enough to be noticeable. Zoom renders keep the old image visible without a loading label.

If a requested zoom is too large for the render budget, Insidious restores the previous raster, zoom, and pan and shows a compact error. Missing, unreadable, corrupt, unsupported, or removed entries show a clear full error while preserving the collection and navigation.

The application does not silently skip a failed current entry. Use previous or next to decide how to continue.
