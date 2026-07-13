# PLANS.md

This project uses planning for complex or risky changes.

## When to use `/plan`

Use `/plan` before tasks that affect architecture, data flow, performance, threading, archive handling, caching, or UI behavior across multiple components.

Examples:

- project skeleton or build-system redesign
- image loading pipeline
- collection/source abstraction
- archive support
- thumbnail/cache subsystem
- fullscreen UI behavior
- large refactors
- dependency selection

Do not require a full plan for trivial edits such as renaming a button, fixing a typo, adding one supported extension, or adjusting a shortcut.

## Plan requirements

A good plan should include:

1. Goal
2. Relevant files to inspect
3. Proposed implementation steps
4. Risks and assumptions
5. Tests or manual checks
6. What will not be changed

## Execution rule

For complex tasks, Codex should first propose the plan and wait for approval before editing files.

## Project-specific planning priorities

When planning, pay special attention to:

- UI responsiveness
- memory usage
- privacy / no traces
- archive streaming vs disk extraction
- image rendering quality
- natural sorting
- handling corrupted or unsupported files
- keeping UI and core logic separate
