# Linux Image Viewer

Рабочее название проекта: **Linux Image Viewer**.

Цель проекта — нативное Linux-приложение для быстрого просмотра изображений, папок и архивов с изображениями через GUI. Программа должна быть быстрой, предсказуемой, аккуратной к памяти и не оставлять следов просмотра.

## Основные документы

- `docs/product.md` — назначение продукта и основные сценарии.
- `docs/requirements.md` — функциональные и нефункциональные требования.
- `docs/mvp.md` — границы первой рабочей версии.
- `docs/ui.md` — структура интерфейса и поведение UI.
- `docs/architecture.md` — предполагаемые модули и границы ответственности.
- `docs/implementation.md` — карта текущей реализации, слои, потоки данных и правила для будущих изменений.
- `docs/roadmap.md` — актуальный backlog: что уже готово, ближайшие задачи и более поздние milestones.
- `docs/risks.md` — сложные места и технические риски.
- `docs/decisions.md` — принятые архитектурные решения.
- `docs/glossary.md` — терминология проекта.

## Agent workflow

- `AGENTS.md` — постоянные инструкции для Codex.
- `PLANS.md` — правила для execution plans и `/plan`.
- `prompts/` — готовые prompts для первых итераций.

## Начальный принцип разработки

Сначала делается маленькая, стабильная версия viewer pipeline: открыть изображение, несколько выбранных изображений, папку или архив, навигация, масштабирование, fullscreen, drag'n'drop, информация, lazy thumbnail panel, базовое сравнение изображений и настройки без следов просмотра. RAW, JPEG XL, display-profile matching и продвинутая настройка rendering добавляются отдельными milestones после стабилизации базового просмотра.

## Текущая реализация

Проект сейчас реализован как C++20 / Qt6 Widgets приложение с libvips/vips-cpp 8.18.3+ для декодирования изображений и libarchive для чтения архивов.

- `src/app/` — запуск процесса, инициализация libvips, создание `QApplication`, обработка optional path argument.
- `src/ui/` — Qt Widgets слой: главное окно, actions/shortcuts, drag'n'drop, statusbar, fullscreen, левая панель дерева/списка файлов, правая панель базовой информации, диалог настроек, widget отрисовки изображения.
- `src/core/` — логика без виджетов: `ImageEntry`, фильтр форматов MVP, natural sort, сканирование папки/архива/нескольких выбранных источников, source tree для папок/архивов/синтетических multi-source root, состояние viewer, настройки, render-memory budget, memory-only cache, libarchive full-entry/stream readers, libvips decoder.
- `tests/` — Qt Test тесты для core-логики.

Главный поток открытия изображения: UI принимает один путь или однородный набор путей, core создаёт одиночную коллекцию, папку/архив source tree или синтетический multi-source root, `ViewerController` выбирает текущий entry, `VipsImageDecoder` асинхронно строит display-sized `QImage`, `MainWindow` обновляет `ImageViewWidget` и statusbar. Смешанные multi-drop наборы (например image+archive) отклоняются с ошибкой. Главное меню скрыто по умолчанию и доступно через `Alt` или правую toolbar menu button; toolbar, shortcuts и меню используют общие actions. Повторные render-запросы для того же изображения схлопываются до последнего запроса, а переключение на другое изображение запускает новый foreground decode сразу. Neighbor prefetch запускается только для fit-sized decode requests, чтобы фоновые задачи не повторяли тяжёлые actual-size/custom zoom renders.

Архивы ZIP/7z/RAR открываются через libarchive как дерево virtual folders. При открытии асинхронно перечисляются headers и строятся списки изображений по папкам; display decode читает выбранный entry как последовательный stream без полной буферизации и без распаковки на диск. Для архивов с паролем пароль запрашивается при открытии, хранится только в памяти для текущего source и очищается при смене source. При открытии нескольких архивов приложение спрашивает пароль один раз, применяет его к выбранным архивам и отменяет открытие, если какой-либо архив его отклоняет.

Левая панель `Files` разделена по вертикали: сверху дерево папок текущего source, снизу вкладки `List` и `Thumbnails` для изображений выбранной папки. Thumbnail generation запускается только при явно открытой вкладке `Thumbnails` и видимой панели, создаёт видимые и ближайшие превью асинхронно, хранит их только в памяти и останавливает очередь при закрытии вкладки/панели или смене source. Выбранная папка становится активной коллекцией `ViewerController`: next/previous идут внутри папки, а на границе переходят к соседней подходящей папке по дереву; first/last остаются внутри активной папки. Обычный выбор строки или thumbnail переключает текущее изображение через существующий viewer pipeline; `Ctrl`/`Shift` multi-select используется для набора изображений на сравнение. Для filesystem folders подпапки загружаются лениво при выборе или раскрытии ветки. Для нескольких выбранных изображений дерево показывает синтетический root с одним плоским списком; для нескольких папок или архивов под синтетическим root находятся top-level источники в порядке выбора.

В normal mode центральная область имеет вкладки `Viewer` и `Compare`. В `List` или `Thumbnails` можно выбрать изображения текущей активной папки через multi-select и использовать контекстное меню `Compare`: `Add to Compare` добавляет 1+ изображение в memory-only compare-сессию, `Open Compare` показывает накопленную сессию, `In Row`, `Grid` и `Carousel` быстро заменяют сессию текущим выделением из 2-8 изображений, а `Hover Switch` запускает режим переключения для ровно двух изображений. Выбранные строки или thumbnail также можно перетащить на открытую вкладку `Compare`; drop использует тот же append-flow, что и `Add to Compare`, и принимает только внутренние элементы текущего source. Так можно собрать изображения из разных папок текущего source без переключения на Compare после каждого добавления; если текущий режим не поддерживает итоговое количество, append блокируется. В Row изображения равномерно делят доступную ширину панели, подгоняются в размер своих tiles, имеют независимый zoom/pan, поддерживают drag'n'drop reorder и удаление tile через `Remove from Compare` или `Delete`. В Grid изображения раскладываются в авто-подобранную сетку внутри размеров панели, не расширяют вкладку шире окна, подгоняются под ячейки и используют те же per-tile zoom/pan/reorder/remove. В Carousel показывается одно изображение за раз из compare-сессии: wheel листает по кругу, `Ctrl+wheel` масштабирует, ЛКМ-drag двигает увеличенное изображение, а `Left`/`Right` и `PageUp`/`PageDown` листают с клавиатуры; текущий слайд можно удалить через context menu или `Delete`. В Hover Switch без наведения на видимую область base image показывается первое изображение, при наведении на неё — второе; оба изображения декодируются под один viewport и используют общий zoom/pan. Compare имеет runtime-only alignment presets: Fit Center, Top Left, Top Center, Center Left, Bottom Left, Bottom Center, Same Source Scale и Cover Center; смена preset сбрасывает текущий compare zoom/pan. Compare-сессия и её cache живут только в памяти и очищаются при смене source; `F11` или ЛКМ double click по compare layout переводит активную compare-сессию в чистый fullscreen без панелей и сохраняет выбранный режим, alignment, порядок, zoom и pan.

`Navigate -> Quick Jump` (`Ctrl+F`) открывает поиск по имени файла в активной папке. Поиск нечувствителен к регистру и переключает выбранное совпадение через тот же viewer pipeline.

`Edit -> Copy Original Image` (`Ctrl+C`) копирует в clipboard исходные bytes текущего image с MIME исходного формата и добавляет совместимое clipboard image data, декодированное из этих же исходных bytes. Команда не использует rendered raster, поэтому zoom, rotate, fit, EXIF autorotate и display scaling не меняют копируемые данные. Для archive entries bytes читаются из архива в память без распаковки на диск; это bounded full-read path, отдельный от streaming display decode.

Правая панель `Info` по умолчанию скрыта и включается через `View -> Info`. Она группирует сведения в компактные секции `Info` и `EXIF`: базовая информация о текущем entry, dimensions после decode, статус color profile и набор полезных EXIF-полей, которые отдаёт libvips.

Viewer fullscreen показывает только область изображения по умолчанию. Панели появляются как overlay поверх изображения при приближении мыши к краю: слева `Files`, справа `Info`, сверху основные actions, снизу status text. Hover-панели не меняют размер viewer и не запускают перерендер изображения; зоны срабатывания по умолчанию — 7% ширины для боковых краёв и 5% высоты для верхнего/нижнего. В левом верхнем углу fullscreen по умолчанию показывается pinned info line: `name (resolution | filesize) [index/count] zoom`; её можно выключить и настроить font, size и color в Preferences. Compare fullscreen intentionally clean: без menu/toolbar/status/docks/tabbar/edge overlays, выход по `Esc`, `F11` или ЛКМ double click по compare layout.

Statusbar и fullscreen bottom overlay используют одну компактную строку: dimensions/MP, file size, file modified date, EXIF date, имя файла, zoom percentage и индекс в активной папке. Поля показываются как приглушённые labels и значения, разделённые вертикальными separators; file date и EXIF date помечены как `modified:` и `taken:` и показываются в формате `YYYY.M.D HH:MM:SS`. В `fit`-режиме zoom показывает фактический displayed scale в процентах.

Верхний toolbar сгруппирован в компактные блоки source, browse, files и view; fullscreen top overlay использует те же действия в тёмной полупрозрачной компактной панели. Кнопка `Sort` в toolbar сортирует список изображений активной папки по name, type, size или date, для каждого режима asc/desc. Сортировка не меняет порядок папок или top-level multi-source roots, не сохраняется в настройки и при запуске всегда начинается с natural name ascending; выбранный runtime-режим применяется к новым папкам/архивам до закрытия приложения. Временная коллекция из нескольких выбранных файлов сначала сохраняет порядок выбора, а явное применение Sort сортирует её как обычный активный список.

`Edit -> Preferences` открывает базовый диалог настроек. Сейчас он сохраняет default zoom mode, поведение zoom/pan при переключении изображений, режим ЛКМ zoom, EXIF autorotate, click zoom multiplier, wheel actions, zoom step, scaling quality preset, embedded color profile conversion, region-based rendering для zoom/actual size, core viewer shortcuts с keyboard или mouse button bindings, fullscreen edge trigger zones, pinned fullscreen info line presentation и запоминание layout. EXIF autorotate также доступен как быстрый toggle в `View` и верхней панели; rotate left/right доступны из `View`, верхней панели, fullscreen top overlay и shortcuts, не меняют исходный файл и сбрасываются при переходе на другое изображение. Настройки пишутся через `QSettings` в стандартное пользовательское config-хранилище Qt и не содержат историю открытых файлов, путей, паролей архивов, thumbnails или decoded cache.

Если текущий файл удалён, стал нечитаемым или не декодируется, viewer показывает ошибку для этого entry, но сохраняет коллекцию и навигацию. Ошибки загрузки не попадают в memory cache.

При переключении heavy images текущий raster остаётся на экране до готовности нового decode result; transient `Loading image...` показывается полупрозрачным overlay поверх изображения только после короткой задержки, чтобы быстрые decode-переходы не мигали loading-надписью. При zoom старый raster тоже остаётся на месте, но loading-надпись не показывается, чтобы изображение не дёргалось визуально; non-fit zoom/actual-size по умолчанию декодирует видимую область плюс запас вместо полного zoomed raster. HiDPI rendering строит physical rasters и хранит DPR в decode cache key; `actual size / 100%` означает один image pixel на один physical screen pixel. Scaling quality выбирается в Preferences как preset libvips kernel; default — `High quality (Lanczos3)`. Embedded ICC profiles по умолчанию конвертируются в sRGB через libvips/lcms2; изображения без профиля считаются sRGB, а при ошибке профиля viewer мягко возвращается к sRGB fallback. Region rendering можно отключить для более плавного pan ценой памяти. Дополнительные zoom/view-transform команды не наслаиваются, пока render не завершился. Если zoom-запрос слишком большой для render budget, viewer возвращает предыдущие zoom/pan/raster и показывает компактный overlay `Too large to render`. Остальные ошибки и пустые состояния по-прежнему очищают изображение и показывают полноценное сообщение.

Для ориентации по коду см. `docs/implementation.md`.

## Управление в MVP

- Wheel — предыдущее/следующее изображение по умолчанию; действие wheel/`Ctrl+wheel`/`Shift+wheel` настраивается в Preferences.
- `Ctrl+wheel` — zoom in/out относительно текущего отображаемого масштаба по умолчанию, с опорой на позицию курсора.
- `Shift+wheel` — no action по умолчанию.
- Next/Previous по кнопкам, shortcuts или wheel по умолчанию сбрасывают масштаб и pan в установленный базовый режим `fit` или `actual size`; это можно переключить на сохранение текущего view в Preferences. На границе активной папки переходят к соседней подходящей папке по дереву, а если листать некуда, текущий image перерендеривается с тем же сбросом.
- Core viewer shortcuts настраиваются в Preferences: команды можно переназначить на клавиатуру и/или кнопку мыши, очистить или сбросить к defaults; дублирующиеся shortcuts не сохраняются. `Ctrl+C` по умолчанию копирует исходные bytes текущего image, `MouseBack` / `MouseForward` переходят к предыдущей/следующей подходящей папке в source tree и выбирают первый файл папки с учетом текущей сортировки.
- ЛКМ по умолчанию работает как click zoom: простой клик включает временный zoom 2x от текущего отображаемого масштаба с центровкой по точке клика, следующий клик после click/rectangle zoom возвращает исходный режим `fit` или `actual size`.
- В режимах ЛКМ zoom magnifier cursor виден в idle-состоянии только когда действие доступно: изображение уже показано, render не ожидается и не активен hold zoom.
- В Preferences можно переключить ЛКМ в hold zoom: зажатие сразу включает временный zoom всего изображения, движение сдвигает увеличенный raster, отпускание возвращает исходный view.
- В non-fit view, включая `actual size`, уже увеличенное или крупное изображение можно двигать рукой через зажатую среднюю кнопку мыши или `Ctrl+ЛКМ`; эти жесты не включают zoom сами по себе.
- Зажатая правая кнопка мыши — прямоугольник zoom-to-selection.
- Double click — fullscreen; temporary click zoom is suppressed for double clicks.
- In viewer fullscreen, edge hover shows overlay panels without leaving fullscreen. In compare fullscreen, only the compare layout is visible. `Esc` exits fullscreen; left double click also toggles compare fullscreen.

## Build

MVP implementation uses C++20, Qt6 Widgets, libvips/vips-cpp 8.18.3+ with lcms2 ICC support when available, and libarchive.

Arch-like systems:

```sh
sudo pacman -S qt6-base libvips libarchive cmake ninja pkgconf
```

Debian/Ubuntu-like systems:

```sh
sudo apt install qt6-base-dev libvips-dev libarchive-dev cmake ninja-build pkg-config
```

Configure, build and test:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Developer memory audit:

```sh
cmake --build build --target insidious_memory_audit
python3 tools/memory_audit.py --build-dir build
```

Run:

```sh
./build/insidious
./build/insidious /path/to/image-folder-or-archive
```

The app intentionally does not use recent-file history, persistent thumbnail caches, persistent comparison sessions, archive extraction, archive password persistence or file logging in the MVP. Thumbnails are generated only while the thumbnail tab is visible and are cached in memory only. Comparison images are decoded asynchronously and cached in memory only for the running process. The settings file stores only non-history preferences such as viewer behavior, shortcuts, fullscreen presentation, normal-mode theme, and layout state.
