# Roadmap

Roadmap — живой backlog реализации, а не обещание релиза. Держим его отсортированным по текущей продуктовой ценности и зависимостям. Если фича переезжает между разделами, нужно обновить `README.md`, `AGENTS.md`, `docs/mvp.md` и `docs/implementation.md`, если они описывают затронутое текущее состояние.

## Готово / MVP foundation

- Документация проекта, build skeleton, C++20 / Qt6 Widgets приложение.
- Базовый viewer: открыть изображение, открыть папку, использовать родительскую папку как контекст при открытии одного файла.
- Viewer controls: `fit to window`, `actual size`, zoom in/out, pan, view-only rotate left/right, fullscreen.
- Input model: wheel navigation, настраиваемые wheel actions, Ctrl+wheel zoom around cursor, toolbar/shortcut zoom around viewport center, left-click temporary zoom/restore, left-button pan, right-button rectangle zoom, double-click fullscreen.
- Настраиваемые режимы ЛКМ zoom: click-to-zoom/restore по умолчанию и hold-to-zoom-pan с возвратом на release.
- Magnifier cursor для режимов ЛКМ zoom показывается только когда zoom/restore или hold-to-zoom доступен.
- Async decode pipeline через libvips: display-sized rasters, stale-result guards, neighbor prefetch, memory-only decoded image cache.
- Zoom render budget guard: если zoom-запрос слишком большой для decode budget, viewer откатывает предыдущий raster/zoom/pan и показывает компактный overlay вместо full-screen ошибки.
- Обработка missing/unreadable/corrupted текущих entries без разрушения коллекции и навигации.
- Source tree `Files`: дерево папок/архива над списком изображений активной папки, lazy filesystem folder loading, cross-folder next/previous.
- Архивы: ZIP/7z/RAR через libarchive, password prompt, без распаковки на диск, пароль только в памяти.
- `Info` panel с компактной информацией об entry и полезными EXIF-полями из libvips metadata.
- Quick Jump по имени файла в активной папке, включая fullscreen in-window overlay.
- True fullscreen chrome: по умолчанию только изображение, edge overlay panels поверх изображения.
- Basic Preferences dialog: viewer defaults, image-switch policy, left-button zoom behavior, wheel actions, zoom step, click zoom multiplier, fullscreen trigger zones, layout persistence.

## Ближайшее / small stabilization

- Settings polish:
  - добавить on/off для EXIF autorotate;
  - добавить default sort mode после появления sort-by-date;
  - сохранять privacy boundary: никаких paths, history, archive passwords, thumbnails, decoded cache или logs.
- Status/fullscreen presentation settings:
  - настройка состава statusbar/fullscreen info line;
  - компактное и локализуемое форматирование.
  - сделать опциональный нескрываемый fullscreen info line в области вывода изображения
- Quick Jump fullscreen polish:
  - сделать fullscreen search dialog компактнее;
  - когда focus в поле поиска, Up/Down должны двигать выделение в result list.
- Clipboard:
  - copy current rendered image to clipboard;
  - отдельно решить, нужна ли команда copy original bytes.
- Rotate:
  - добавить возможность поворота изображения кнопками по/против часовой стрелки на 90 градусов


## Среднесрочные features

- Sorting:
  - sort by modification date для filesystem sources;
  - fallback для архивов, когда надёжная дата entry недоступна.
- Thumbnail panel, optional:
  - lazy async generation;
  - memory-only thumbnail cache;
  - отмена thumbnail work при смене panel/source.
- Performance and memory audit:
  - проверить cache eviction на очень больших изображениях;
  - проверить lazy-load behavior на больших filesystem trees и больших архивах;
  - держать long-running work вне UI thread.
- Source completeness:
  - поддержать несколько выбранных файлов и multi-file drag'n'drop как временную коллекцию;
  - сохранить совместимость этого source type с текущими viewer/source-tree concepts.

## Позже / larger milestones

- Zoom UX polish:
  - заменить full-raster zoom rendering на region-based rendering: декодировать и масштабировать только видимую область изображения плюс небольшой запас, чтобы убрать ранний упор в render budget на больших исходниках.
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
- Configurable hotkeys:
  - shortcut editor;
  - conflict detection;
  - restore defaults.
- Packaging:
  - документировать runtime dependencies по дистрибутивам;
  - подготовить Linux packaging path после стабилизации viewer behavior.
- UI:
  - Переделать кнопки
