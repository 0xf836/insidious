# Distribution Target Matrix

`platforms.json` is the canonical machine-readable inventory of native target decisions. Schema version 1 records candidates, exclusions, diagnostic evidence, and remaining qualification. It is input for future CI and packaging work, not a declaration that packages currently exist.

- `selected_for_future_qualification` chooses the intended Arch/Fedora test scope. Fedora 44 is an explicit candidate, not an alias for whichever release is latest later.
- `native_qualified` requires successful clean-system builds and behavioral acceptance using standard packages. All entries currently set it to `false`.
- `release_required` identifies mandatory release targets only after qualification. Future publication must reject an empty qualified target set; the current all-false inventory cannot authorize a release.
- `build_validation` distinguishes local tests, unrun candidates, and a diagnostic build with a dependency constraint override. A diagnostic override never qualifies a native target.
- `image_digest` is null when no target image was tested. It is not permissible to invent a digest or infer qualification from a package listing.
- `artifact_kind` states the intended package type. Recipes, versioned filenames, image selection for pending targets, CI consumption, and publication validation belong to later distribution stages.

The owner approved completing the first stage with dependency evidence, targeted local tests, and this matrix, deferring clean Arch/Fedora qualification to CI/package preparation. `audit_complete` records that agreed scope; `native_qualification_complete` and `release_ready` remain false. Before shipping, qualify the selected candidates, record frozen environments and package inventories, and update required targets through review.

See the [dependency audit](../docs/dependency-audit.md) and [release specification](../docs/releasing.md) for evidence and acceptance gates. This file is developer documentation and is not added to current installed documentation rules; future package jobs consume the source checkout/archive.
