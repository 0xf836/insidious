/plan

Read:

- AGENTS.md
- docs/mvp.md
- docs/architecture.md
- docs/requirements.md

Goal: implement opening and displaying a single image file.

Scope:

- Support only regular filesystem image files.
- Do not implement folder navigation yet.
- Do not implement archives.
- Do not implement thumbnails.
- Do not implement EXIF panel.

Requirements:

- The app can open one image from a file picker or command-line argument, depending on what already exists in the skeleton.
- The image is displayed in the main viewer area.
- The UI does not crash on missing, unreadable, corrupted, or unsupported files.
- Show a clear error message on failure.
- Keep image loading separated from UI code as much as practical.
- Add tests for non-GUI logic where practical.

Before editing, propose a short plan and list the files you expect to change.
