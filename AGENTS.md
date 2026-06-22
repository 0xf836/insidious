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
- Keep GUI code separate from core image, collection, archive, metadata, and cache logic.
- Keep long-running work off the UI thread.
- Add or update tests for non-trivial core logic.
- After changes, run the relevant build/test command if it exists.
- If a requirement is ambiguous, stop and ask for clarification or write down the assumption before implementing.

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

## Definition of done

A task is done when:

- The requested behavior is implemented and scoped to the task.
- The app still builds/runs.
- Relevant tests are added or updated where practical.
- Error handling is present for common failure modes.
- The final response explains what changed, how it was tested, and what was intentionally left out.
