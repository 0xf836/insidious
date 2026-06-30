# Roadmap

Roadmap — живой backlog реализации, а не обещание релиза. Держим его отсортированным по текущей продуктовой ценности и зависимостям. Если фича переезжает между разделами, нужно обновить `README.md`, `AGENTS.md`, `docs/mvp.md` и `docs/implementation.md`, если они описывают затронутое текущее состояние.

## Готово / MVP foundation

- Документация проекта, build skeleton, C++20 / Qt6 Widgets приложение.
- Базовый viewer: открыть изображение, открыть папку, использовать родительскую папку как контекст при открытии одного файла.
- Viewer controls: `fit to window`, `actual size`, zoom in/out, pan, view-only rotate left/right, fullscreen.
- Input model: wheel navigation, настраиваемые wheel actions, Ctrl+wheel zoom around cursor, toolbar/shortcut zoom around viewport center, left-click temporary zoom/restore, left-button pan, right-button rectangle zoom, double-click fullscreen.
- Настраиваемые режимы ЛКМ zoom: click-to-zoom/restore по умолчанию и hold-to-zoom-pan с возвратом на release.
- Magnifier cursor для режимов ЛКМ zoom показывается только когда zoom/restore или hold-to-zoom доступен.
- Async decode pipeline через libvips: display-sized rasters, stale-result guards, same-entry render coalescing, immediate foreground decode on image switch, tracked/coalesced neighbor prefetch with cache-generation guarded writes, QImage cleanup ownership for libvips output buffers, memory-only decoded image cache.
- Zoom render budget guard: если zoom-запрос слишком большой для decode budget, viewer откатывает предыдущий raster/zoom/pan и показывает компактный overlay вместо full-screen ошибки.
- Total peak memory audit tooling and baseline for large filesystem image, archive image, zoom render, and prefetch scenarios.
- Обработка missing/unreadable/corrupted текущих entries без разрушения коллекции и навигации.
- Source tree `Files`: дерево папок/архива над списком изображений активной папки, lazy filesystem folder loading, cross-folder next/previous.
- Архивы: ZIP/7z/RAR через libarchive, password prompt, без распаковки на диск, пароль только в памяти.
- `Info` panel с компактной информацией об entry и полезными EXIF-полями из libvips metadata.
- Quick Jump по имени файла в активной папке, включая fullscreen in-window overlay.
- True fullscreen chrome: по умолчанию только изображение, edge overlay panels поверх изображения.
- Compact status/fullscreen line formatter: labelled dimensions/MP, file size, file date, EXIF date, filename, zoom, index/count.
- Optional pinned fullscreen info line: `name (resolution | filesize) [index/count] zoom`, separate from edge overlays, with fullscreen settings for visibility, font, size, and color.
- Clipboard: copy current original image bytes to clipboard with the source image MIME type.
- Runtime file sorting from the toolbar: name natural order, type, size, and date; each supports ascending/descending without persistent settings.
- Basic Preferences dialog and quick actions: viewer defaults, image-switch policy, left-button zoom behavior, EXIF autorotate, wheel actions, zoom step, click zoom multiplier, explicit previous/next folder actions, configurable core viewer keyboard/mouse shortcuts with conflict detection and restore defaults, fullscreen trigger zones, layout persistence.

## Ближайшее / small stabilization

- Memory follow-ups after total peak audit:
  - prefetch policy: не запускать speculative prefetch для expensive render requests; оставить prefetch преимущественно для base/fit-sized neighbor images, чтобы фоновые decode jobs не дублировали zoom/archive memory pressure;
  - archive decode budget: учитывать in-memory archive entry buffer вместе с render output budget и показывать понятную ошибку, если archive entry + decode/render state выходят за допустимый предел;
  - audit coverage: при появлении thumbnail panel добавить memory-audit сценарии для thumbnail generation/cache до включения больших thumbnail caches.
- Settings polish:
  - сохранять privacy boundary: никаких paths, history, archive passwords, thumbnails, decoded cache или logs.
- Quick Jump fullscreen polish:
  - сделать fullscreen search dialog компактнее;
  - когда focus в поле поиска, Up/Down должны двигать выделение в result list.

## Среднесрочные features

- Thumbnail panel, optional:
  - lazy async generation;
  - memory-only thumbnail cache;
  - отмена thumbnail work при смене panel/source.
- Performance and memory improvements:
  - region-based zoom rendering: заменить full-raster zoom decode на decode/render только видимой области изображения плюс небольшой запас; это главный structural follow-up после memory audit, где `zoom-render` дал самый высокий measured peak RSS;
  - archive random-access/performance strategy: benchmark current per-image archive decode on large ZIP/7z/RAR, reduce repeated header scans where possible, consider limiting archive prefetch until access strategy is improved;
  - large tree UI scaling: не пересобирать preorder folder lists на каждый action-state update, не делать full `CollectionTreeModel` reset при lazy-load одной папки, рассмотреть path-to-node/index maps.
- Source completeness:
  - поддержать несколько выбранных файлов и multi-file drag'n'drop как временную коллекцию;
  - сохранить совместимость этого source type с текущими viewer/source-tree concepts.

## Позже / larger milestones

- Zoom UX polish:
  - добавить будущий отдельный режим temporary view: при удержании ЛКМ zoom следует за курсором (размеры области настраиваются), после отпускания возврат к исходному состоянию.
- Advanced rendering:
  - ICC/color management;
  - configurable scaling/filter quality;
  - HiDPI correctness audit.
- Advanced formats:
  - AVIF;
  - JPEG XL;
  - TIFF, если libvips support надёжен в target environments;
  - RAW как отдельный research-heavy milestone.
- Advanced shortcut features:
  - expand customization beyond core viewer commands to every useful `QAction`;
  - profiles/import-export if real user demand appears;
  - configurable shortcut scopes if global/window/widget ambiguity becomes a problem.
- Packaging:
  - документировать runtime dependencies по дистрибутивам;
  - подготовить Linux packaging path после стабилизации viewer behavior.
- UI:
  - Переделать кнопки
