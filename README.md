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

Сначала делается маленькая, стабильная версия viewer pipeline: открыть изображение, папку или архив, навигация, масштабирование, fullscreen, базовый drag'n'drop, информация и настройки без следов просмотра. RAW, JPEG XL, thumbnail panel и глубокая настройка rendering добавляются отдельными milestones после стабилизации базового просмотра.

## Текущая реализация

Проект сейчас реализован как C++20 / Qt6 Widgets приложение с libvips/vips-cpp для декодирования изображений и libarchive для чтения архивов.

- `src/app/` — запуск процесса, инициализация libvips, создание `QApplication`, обработка optional path argument.
- `src/ui/` — Qt Widgets слой: главное окно, actions/shortcuts, drag'n'drop, statusbar, fullscreen, левая панель дерева/списка файлов, правая панель базовой информации, диалог настроек, widget отрисовки изображения.
- `src/core/` — логика без виджетов: `ImageEntry`, фильтр форматов MVP, natural sort, сканирование папки/архива, source tree для папок/архивов, состояние viewer, настройки, render-memory budget, memory-only cache, libarchive full-entry/stream readers, libvips decoder.
- `tests/` — Qt Test тесты для core-логики.

Главный поток открытия изображения: UI принимает путь, core создаёт одиночную коллекцию, сканирует папку или читает список изображений из архива, `ViewerController` выбирает текущий entry, `VipsImageDecoder` асинхронно строит display-sized `QImage`, `MainWindow` обновляет `ImageViewWidget` и statusbar. Повторные render-запросы для того же изображения схлопываются до последнего запроса, а переключение на другое изображение запускает новый foreground decode сразу. Neighbor prefetch запускается только для fit-sized decode requests, чтобы фоновые задачи не повторяли тяжёлые actual-size/custom zoom renders.

Архивы ZIP/7z/RAR открываются через libarchive как дерево virtual folders. При открытии асинхронно перечисляются headers и строятся списки изображений по папкам; display decode читает выбранный entry как последовательный stream без полной буферизации и без распаковки на диск. Для архивов с паролем пароль запрашивается при открытии, хранится только в памяти для текущего архива и очищается при смене source.

Левая панель `Files` разделена по вертикали: сверху дерево папок текущего source, снизу список изображений выбранной папки без thumbnails. Выбранная папка становится активной коллекцией `ViewerController`: next/previous идут внутри папки, а на границе переходят к соседней подходящей папке по дереву; first/last остаются внутри активной папки. Выбор строки мышью или клавиатурой переключает текущее изображение через существующий viewer pipeline. Для filesystem folders подпапки загружаются лениво при выборе или раскрытии ветки.

`Navigate -> Quick Jump` (`Ctrl+F`) открывает поиск по имени файла в активной папке. Поиск нечувствителен к регистру и переключает выбранное совпадение через тот же viewer pipeline.

`Edit -> Copy Original Image` (`Ctrl+C`) копирует в clipboard исходные bytes текущего image с MIME исходного формата и добавляет совместимое clipboard image data, декодированное из этих же исходных bytes. Команда не использует rendered raster, поэтому zoom, rotate, fit, EXIF autorotate и display scaling не меняют копируемые данные. Для archive entries bytes читаются из архива в память без распаковки на диск; это bounded full-read path, отдельный от streaming display decode.

Правая панель `Info` по умолчанию скрыта и включается через `View -> Info`. Она группирует сведения в компактные секции `Info` и `EXIF`: базовая информация о текущем entry, dimensions после decode и набор полезных EXIF-полей, которые отдаёт libvips.

Fullscreen показывает только область изображения по умолчанию. Панели появляются как overlay поверх изображения при приближении мыши к краю: слева `Files`, справа `Info`, сверху основные actions, снизу status text. Hover-панели не меняют размер viewer и не запускают перерендер изображения; зоны срабатывания по умолчанию — 7% ширины для боковых краёв и 5% высоты для верхнего/нижнего. В левом верхнем углу fullscreen по умолчанию показывается pinned info line: `name (resolution | filesize) [index/count] zoom`; её можно выключить и настроить font, size и color в Preferences. Default preset: `DejaVu Sans`, `10 pt`, `#00ff00`.

Statusbar и fullscreen bottom overlay используют одну компактную строку: dimensions/MP, file size, file modified date, EXIF date, имя файла, zoom percentage и индекс в активной папке. Поля показываются как приглушённые labels и значения, разделённые вертикальными separators; file date и EXIF date помечены как `modified:` и `taken:` и показываются в формате `YYYY.M.D HH:MM:SS`. В `fit`-режиме zoom показывает фактический displayed scale в процентах.

Верхний toolbar сгруппирован в компактные блоки source, browse, files и view; fullscreen top overlay использует те же действия в тёмной полупрозрачной компактной панели. Кнопка `Sort` в toolbar сортирует список изображений активной папки по name, type, size или date, для каждого режима asc/desc. Сортировка не меняет порядок папок, не сохраняется в настройки и при запуске всегда начинается с natural name ascending; выбранный runtime-режим применяется к новым папкам/архивам до закрытия приложения.

`Edit -> Preferences` открывает базовый диалог настроек. Сейчас он сохраняет default zoom mode, поведение zoom/pan при переключении изображений, режим ЛКМ zoom, EXIF autorotate, click zoom multiplier, wheel actions, zoom step, region-based rendering для zoom/actual size, core viewer shortcuts с keyboard или mouse button bindings, fullscreen edge trigger zones, pinned fullscreen info line presentation и запоминание layout. EXIF autorotate также доступен как быстрый toggle в `View` и верхней панели; rotate left/right доступны из `View`, верхней панели, fullscreen top overlay и shortcuts, не меняют исходный файл и сбрасываются при переходе на другое изображение. Настройки пишутся через `QSettings` в стандартное пользовательское config-хранилище Qt и не содержат историю открытых файлов, путей, паролей архивов, thumbnails или decoded cache.

Если текущий файл удалён, стал нечитаемым или не декодируется, viewer показывает ошибку для этого entry, но сохраняет коллекцию и навигацию. Ошибки загрузки не попадают в memory cache.

При переключении heavy images текущий raster остаётся на экране до готовности нового decode result; transient `Loading image...` показывается полупрозрачным overlay поверх изображения только после короткой задержки, чтобы быстрые decode-переходы не мигали loading-надписью. При zoom старый raster тоже остаётся на месте, но loading-надпись не показывается, чтобы изображение не дёргалось визуально; non-fit zoom/actual-size по умолчанию декодирует видимую область плюс запас вместо полного zoomed raster. В Preferences это можно отключить для более плавного pan ценой памяти. Дополнительные zoom/view-transform команды не наслаиваются, пока render не завершился. Если zoom-запрос слишком большой для render budget, viewer возвращает предыдущие zoom/pan/raster и показывает компактный overlay `Too large to render`. Остальные ошибки и пустые состояния по-прежнему очищают изображение и показывают полноценное сообщение.

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
- В базовом `fit`-режиме уже увеличенное изображение можно двигать рукой через зажатую среднюю кнопку мыши или `Ctrl+ЛКМ`; эти жесты не включают zoom сами по себе.
- Зажатая правая кнопка мыши — прямоугольник zoom-to-selection.
- Double click — fullscreen; temporary click zoom is suppressed for double clicks.
- In fullscreen, edge hover shows overlay panels without leaving fullscreen; `Esc` exits fullscreen.

## Build

MVP implementation uses C++20, Qt6 Widgets, libvips/vips-cpp and libarchive.

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

The app intentionally does not use recent-file history, persistent thumbnail caches, archive extraction, archive password persistence or file logging in the MVP. The settings file stores only non-history preferences such as viewer behavior, shortcuts, fullscreen presentation, normal-mode theme, and layout state.
