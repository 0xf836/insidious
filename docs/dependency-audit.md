# Dependency Compatibility Audit

This document is the canonical evidence record for the current Qt/libvips minimums. The [release specification](releasing.md#dependency-qualification-and-native-support) defines the remaining native-platform acceptance gates. Distribution stage 1 is complete under the owner-approved scope: dependency evidence, targeted local regression tests, and an explicit platform inventory. Clean distribution qualification is deferred to CI/package preparation; native support, release packages and desktop acceptance are not claimed. The canonical machine-readable inventory is [packaging/platforms.json](../packaging/platforms.json), with its contract in [packaging/README.md](../packaging/README.md).

## Decisions

- Require Qt >= 6.6 in CMake and the Arch VCS recipe. The current unconditional `QEvent::DevicePixelRatioChange` use requires that version. Ubuntu 24.04 with standard Qt 6.4.2 is excluded; an older-Qt compatibility path is not planned.
- Retain libvips/vips-cpp >= 8.18.3, also expressed in the Arch recipe. MKS2021 exists in 8.17.0; relevant SIMD resize fixes, rather than the introduction of that kernel, justify the retained floor.
- Do not qualify Ubuntu 26.04's current standard libvips 8.18.0 package. Functional tests pass in an experimental copy, but relevant upstream resize fixes are absent. Disabling SIMD was investigated and rejected because it materially slows resizing.
- Keep all four presets and existing image, archive, EXIF, ICC, HiDPI, region-rendering, memory, and privacy behavior. No production decoder workaround, global SIMD override, private library, replacement system library, or additional dependency was introduced.

Qt 6.6 is an API lower bound established from source and upstream documentation. Tests here ran with Qt 6.10.2 and 6.12.0; they do not claim an exact Qt 6.6 build has been qualified.

## API and fix evidence

`src/ui/main_window.cpp` handles `QEvent::DevicePixelRatioChange` to update DPR and reload the raster. Qt documents the event as introduced in 6.6. The previous 6.5 requirement admitted a library without the referenced enum. `QImage::deviceIndependentSize()` dates to 6.2 and does not establish a 6.5 floor. Sources: [QEvent](https://doc.qt.io/qt-6/qevent.html#Type-enum), [QImage](https://doc.qt.io/qt-6.5/qimage.html#deviceIndependentSize).

`src/core/vips_image_decoder.cpp` directly selects linear, Mitchell, Lanczos3, and MKS2021 kernels. The [8.17 announcement](https://www.libvips.org/2025/06/05/What's-new-in-8.17.html) and [tagged 8.17.0 header](https://raw.githubusercontent.com/libvips/libvips/v8.17.0/libvips/include/vips/resample.h) establish that MKS2021 predates the historical 8.18.3 requirement added by `059020a`.

However, [8.18.1](https://github.com/libvips/libvips/releases/tag/v8.18.1) fixes a possible out-of-bounds read in the Highway horizontal shrink path, and [8.18.3](https://github.com/libvips/libvips/releases/tag/v8.18.3) fixes one in horizontal reduction. Comparing the upstream tags shows that the vector paths request an extra input pixel so a full SIMD vector can be read safely:

- [8.18.0 shrinkh.c](https://raw.githubusercontent.com/libvips/libvips/v8.18.0/libvips/resample/shrinkh.c) versus [8.18.3 shrinkh.c](https://raw.githubusercontent.com/libvips/libvips/v8.18.3/libvips/resample/shrinkh.c): the vector generator requests `(r->width + 1) * shrink->hshrink` rather than `r->width * shrink->hshrink`.
- [8.18.0 reduceh.cpp](https://raw.githubusercontent.com/libvips/libvips/v8.18.0/libvips/resample/reduceh.cpp) versus [8.18.3 reduceh.cpp](https://raw.githubusercontent.com/libvips/libvips/v8.18.3/libvips/resample/reduceh.cpp): the requested width similarly includes an extra output position's input span.

These operators participate in ordinary downscaling of supported images. Ubuntu's actual source package, obtained through `apt-get source vips` after enabling the standard source repositories, contains the old width expressions at `shrinkh.c:302` and `reduceh.cpp:359`; there is no `debian/patches` directory. The installed package links Highway 1.3.0. The relevant backports are absent, not merely obscured by an older upstream version string.

This is source/fix evidence and a reason to reject a downgrade; no application crash or exploit was reproduced. Other changelog entries must not be blanket-labelled as reachable viewer vulnerabilities. For example, the EXIF IFD validation change is in the metadata update/serialization path, while the viewer reads metadata; its advisory alone does not prove a viewer decode is affected. Sources: [upstream EXIF fix](https://github.com/libvips/libvips/commit/91ebd4d), [8.18.0 EXIF implementation](https://raw.githubusercontent.com/libvips/libvips/v8.18.0/libvips/foreign/exif.c).

## Ubuntu 26.04 experiment

Used the official `ubuntu:26.04` amd64 image with digest `sha256:6a06111c3b0a8600feadb270aec771dc239bf407b25625f93f44ab00e11f4d3c`. The image identifies as Ubuntu 26.04.1 LTS. Packages were resolved on the audit date from standard `resolute`, `resolute-updates`, `resolute-security`, and `resolute-backports`; this is a dated observation, not an immutable apt repository snapshot.

| Component | Installed package version |
| --- | --- |
| GCC / g++ | Compiler 15.2.0; `g++` metapackage `4:15.2.0-5ubuntu1` |
| CMake | `4.2.3-2ubuntu2` |
| Qt | `6.10.2+dfsg-7` |
| libvips / vips-cpp | `8.18.0-1build1` |
| libarchive | `3.8.5-1ubuntu2.3` |
| Highway | `1.3.0-2` |
| lcms2 | `2.17-1ubuntu0.2` |
| libexif | `0.6.25-2ubuntu0.1` |
| libjpeg-turbo | `2.1.5-4ubuntu4` |
| libpng | `1.6.57-1ubuntu0.1` |
| libwebp | `1.5.0-0.1build1` |
| ImageMagick core | `8:7.1.2.18+dfsg1-1` |

Mounted the repository read-only at `/src`, copied the build inputs into `/work`, and changed only that experimental copy's pkg-config gate to `vips-cpp>=8.18.0`. Configured a Debug Ninja build, built all targets with four jobs, and ran CTest with `LC_ALL=C.UTF-8`. No dependency was built from source or installed outside the package manager. `ldd` resolved Qt, libvips, and libarchive from `/usr/lib/x86_64-linux-gnu`; headless `insidious --version` returned `Insidious 0.1.0`.

All 8 CTest entries passed, including the dedicated HiDPI entry. Core tests reported 92 passes and no skips. The normal image-widget entry intentionally skips its HiDPI-environment assertion; the dedicated entry executes it.

## Disabling SIMD: rejected workaround

The table below is a fresh run made after independent review. It supersedes the initial median-only measurements, whose individual samples were not retained. The committed [JSON evidence](evidence/vips-vector-ubuntu2604.json) includes all 96 individual timings (including warmups), CPU model/flags, CPU affinity, OS/kernel, libvips version, and built-in/supported libvips SIMD target masks. Timing variation between runs is expected.

The exact generator and measurement loop are maintained in [tools/vips_vector_benchmark.py](../tools/vips_vector_benchmark.py). Run inside the Ubuntu environment described above:

```sh
python3 /src/tools/vips_vector_benchmark.py --output /tmp/vips-vector-results.json
```

For a local checkout, use `python3 tools/vips_vector_benchmark.py --output /tmp/vips-vector-results.json`. The output path must not exist. This developer-only tool requires Python 3 and the system libvips shared library/CLI (`libvips-tools` on Ubuntu); it adds no application dependency or installed package contents. Synthetic image files use a temporary directory and are removed automatically. The explicit JSON output remains for inspection.

Measured the standard Ubuntu libvips executable with SIMD enabled and with `VIPS_NOVECTOR=1`. `VIPS_VECTOR=0` is **not** equivalent: that variable specifies a mask of targets to disable. The upstream [vector implementation](https://raw.githubusercontent.com/libvips/libvips/v8.18.0/libvips/iofuncs/vector.cpp) confirms these semantics.

Generated a deterministic 6000 x 4000 RGB gradient, materialized it as a native `.v` input, and timed `vips resize input.v output.v SCALE --kernel KERNEL` using `time.perf_counter()` around the subprocess. Used `VIPS_CONCURRENCY=4`, alternating SIMD/scalar order over six pairs, dropping the first pair and reporting medians of five measurements. Times include CLI startup and native-file I/O; these are local synthetic resize measurements, not application navigation benchmarks or universal slowdown estimates.

| Preset | Scale | SIMD median | SIMD disabled median | Ratio |
| --- | --- | --- | --- | --- |
| Linear | 12.5% | 56 ms | 87 ms | 1.56x |
| Linear | 62.5% | 46 ms | 120 ms | 2.59x |
| Mitchell | 12.5% | 62 ms | 96 ms | 1.54x |
| Mitchell | 62.5% | 46 ms | 151 ms | 3.32x |
| Lanczos3 | 12.5% | 57 ms | 107 ms | 1.88x |
| Lanczos3 | 62.5% | 49 ms | 193 ms | 3.97x |
| MKS2021 | 12.5% | 66 ms | 125 ms | 1.91x |
| MKS2021 | 62.5% | 59 ms | 264 ms | 4.48x |

Disabling SIMD would preserve the preset choices but materially penalize the viewer's primary resize path. It also would not itself address unrelated old-backend defects. No suitable alternative small adaptation has been identified. This experiment rejects global SIMD disabling; it does not prove that every possible compatibility adaptation is impossible. Bundling a patched private libvips or replacing the distribution library remains outside the native-package policy. Reconsider Ubuntu when standard repositories carry a fixed upstream release or documented backports, or qualify a separately specified alternative implementation with performance evidence.

## Regression coverage and limits

Expanded `tests/core_tests.cpp` with:

- All four kernels at 12.5%, 62.5%, 150%, and 300% scales, with full pixel comparison against independently selected backend kernels. This tests the preset mapping; it is not an independent validation of libvips kernel mathematics.
- JPEG, PNG, WebP, BMP, and DIB-extension inputs through filesystem, ZIP, and 7z sources, including output geometry and representative color checks. DIB fixtures have BMP headers; headerless DIB is not qualified here.
- A directly injected invalid JPEG ICC profile that must remain viewable with sRGB output. libvips can internally substitute sRGB without throwing; the test does not require a particular fallback-status enum.
- EXIF orientation 6 enabled/disabled combined with each view rotation and HiDPI region rendering, using an asymmetric color gradient. The test independently derives the panned crop rectangle and checks nine pixels against inverse-rotated source coordinates with a five-level JPEG tolerance.

Sensitivity was checked in the disposable Ubuntu source copy: forcing every preset to Lanczos3 caused eight pixel-reference failures; skipping the 180-degree view rotation caused two spatial-reference failures. Both mutations were restored, then the final suite was rerun. No production decoder change was retained.

Existing tests continue to cover embedded ICC conversion, decoded-buffer ownership, bounded/streaming archive access, render-memory failures, cache generation guards, and prefetch policy. Current Arch (Qt 6.12.0, libvips 8.18.7, libarchive 3.8.10, GCC 16.2.1, CMake 4.4.5) also built successfully and passed all 8 CTest entries with 92 core passes.

### Stage 1 completion and remaining release gates

The owner deferred new clean-distribution builds. Added local coverage includes:

- Original, reproducibly generated stored RAR4/RAR5 image archives: filtering, nested tree, bounded original-byte reads, sequential stream equivalence, and pixel-correct decode. See [fixture provenance](../tests/fixtures/README.md). Compressed, solid, multivolume and encrypted RAR are not qualified by these fixtures.
- Bottom-up/top-down 24-bit BMP-family `.dib` files through file and ZIP paths, checking row orientation. Headerless DIB succeeds on the current file-backed loader but is rejected with a clear error by the archive streaming path. This is an existing backend limitation, not a newly added raw DIB reader. A tiny 3 x 2 BMP fixture also exposed archive sniffing failure while file decode succeeded; the passing orientation fixture is 3 x 8. Small BMP archive decode remains a known issue.
- `main_window_async_tests.cpp` completes controlled `QPromise` futures through real Qt watcher callbacks. It checks current-result application, stale decode rejection after image/source transitions, stale errors and cancellation without replacing the displayed raster or populating the cache, stale source-scan rejection and stale lazy-folder-load rejection. Current-result controls verify that the handlers still apply valid work. It does not test CPU-level interruption of libvips or every loader scheduling interleaving.

The UI test uses temporary settings and a narrow test friend, without adding a production decoder injection API. CMake shares the UI source list with the test executable; application layering and behavior are unchanged.

The initial matrix selects Arch rolling and **Fedora 44** as explicit candidates for later qualification. All `native_qualified` and `release_required` flags remain false; `native_qualification_complete` and `release_ready` are false. No release can use an empty qualified target set. Ubuntu 24.04 and Debian 13 are excluded; Ubuntu 26.04 remains blocked. The earlier Ubuntu diagnostic run and original eight-target results above remain historical evidence; the expanded suite was run locally only.

Clean Arch/Fedora builds, exact Qt 6.6 testing, broader archive fixtures, interactive X11/Wayland acceptance, native package installation/upgrade/removal and hosted CI remain later gates. Fuzzing and sanitizer campaigns were not performed. Existing password/archive and zoom issues remain separately tracked in the roadmap.

Local completion validation: all 9 CTest entries pass, with 102 core checks and 11 MainWindow async checks (including Qt Test setup/cleanup). No new distribution build was run. Fixture regeneration is deterministic in the recorded local environment.
