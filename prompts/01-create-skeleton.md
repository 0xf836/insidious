/plan

Read these files first:

- AGENTS.md
- PLANS.md
- docs/product.md
- docs/mvp.md
- docs/architecture.md
- docs/roadmap.md

Goal: create the initial project skeleton for the Linux image viewer.

Important constraints:

- Do not implement full image viewing yet.
- Create only a minimal runnable application with an empty main window.
- Keep the diff small and reviewable.
- Add placeholder modules/classes/interfaces for the planned architecture where appropriate.
- Do not add archive support yet.
- Do not add thumbnail support yet.
- Do not add advanced metadata support yet.

The skeleton should prepare for these future components:

- app bootstrap;
- main window / UI layer;
- viewer state/controller;
- collection source abstraction;
- folder collection source;
- image loader/decoder;
- metadata reader;
- memory cache;
- settings.

Before editing, propose a plan with:

1. files to create;
2. minimal dependencies;
3. build/run commands;
4. what will be intentionally left unimplemented.

Wait for approval before making changes.
