# Distribution Target Matrix

`platforms.json` is the canonical machine-readable inventory of native target decisions. Schema version 1 records candidates, exclusions, diagnostic evidence, and remaining qualification. CI consumes its selected candidates; packaging and release inventory validation remain later work. It does not declare that packages currently exist.

- `selected_for_future_qualification` chooses the intended Arch/Fedora test scope. Fedora 44 is an explicit candidate, not an alias for whichever release is latest later.
- `native_qualified` requires successful clean-system builds and behavioral acceptance using standard packages. All entries currently set it to `false`.
- `release_required` identifies mandatory release targets only after qualification. Future publication must reject an empty qualified target set; the current all-false inventory cannot authorize a release.
- `build_validation` distinguishes host-local tests, passing clean-container tests, unrun candidates, and a diagnostic build with a dependency constraint override. A diagnostic override never qualifies a native target.
- `ci_image` pins each selected CI candidate to an official repository and full digest. `image_digest` must agree with it.
- `image_digest` is null when no target image was tested. It is not permissible to invent a digest or infer qualification from a package listing.
- `artifact_kind` states the intended package type. Recipes, versioned filenames and publication validation belong to later distribution stages. Build workflows are documented in [Continuous integration](../docs/ci.md).

`audit_complete` covers dependency analysis and the initial platform inventory. `native_qualification_complete` and `release_ready` remain false until package and desktop qualification is complete. Promote release targets only after those gates pass. Keep individual build logs, test results and resolved package inventories in Actions artifacts or the ignored `.ci-results/` directory.

See the [dependency audit](../docs/dependency-audit.md) and [release specification](../docs/releasing.md) for evidence and acceptance gates. This file is developer documentation and is not added to current installed documentation rules; future package jobs consume the source checkout/archive.
