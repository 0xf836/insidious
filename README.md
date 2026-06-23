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
- `docs/roadmap.md` — порядок разработки.
- `docs/risks.md` — сложные места и технические риски.
- `docs/decisions.md` — принятые архитектурные решения.
- `docs/glossary.md` — терминология проекта.

## Agent workflow

- `AGENTS.md` — постоянные инструкции для Codex.
- `PLANS.md` — правила для execution plans и `/plan`.
- `prompts/` — готовые prompts для первых итераций.

## Начальный принцип разработки

Сначала делается маленькая, стабильная версия: открыть изображение, открыть папку или архив, навигация, масштабирование, fullscreen, базовый drag'n'drop. RAW, JPEG XL, глубокая настройка UI и сложные панели добавляются после стабилизации базового viewer pipeline.

## Текущая реализация

Проект сейчас реализован как C++20 / Qt6 Widgets приложение с libvips/vips-cpp для декодирования изображений и libarchive для чтения архивов.

- `src/app/` — запуск процесса, инициализация libvips, создание `QApplication`, обработка optional path argument.
- `src/ui/` — Qt Widgets слой: главное окно, actions/shortcuts, drag'n'drop, statusbar, fullscreen, левая панель списка файлов, правая панель базовой информации, widget отрисовки изображения.
- `src/core/` — логика без виджетов: `ImageEntry`, фильтр форматов MVP, natural sort, сканирование папки/архива, состояние viewer, memory-only cache, libarchive reader, libvips decoder.
- `tests/` — Qt Test тесты для core-логики.

Главный поток открытия изображения: UI принимает путь, core создаёт одиночную коллекцию, сканирует папку или читает список изображений из архива, `ViewerController` выбирает текущий entry, `VipsImageDecoder` асинхронно строит display-sized `QImage`, `MainWindow` обновляет `ImageViewWidget` и statusbar.

Архивы ZIP/7z/RAR открываются через libarchive как обычная коллекция изображений. Entries читаются в память при decode и не распаковываются на диск. Для архивов с паролем пароль запрашивается при открытии, хранится только в памяти для текущего архива и очищается при смене source.

Левая панель `Files` показывает текущую коллекцию без thumbnails. Список синхронизируется с `ViewerController`: next/previous/first/last обновляют выделение, а выбор строки мышью или клавиатурой переключает текущее изображение через существующий viewer pipeline.

`Navigate -> Quick Jump` (`Ctrl+F`) открывает поиск по имени файла в текущей коллекции. Поиск нечувствителен к регистру и переключает выбранное совпадение через тот же viewer pipeline.

Правая панель `Info` по умолчанию скрыта и включается через `View -> Info`. Она группирует сведения в компактные секции `Info` и `EXIF`: базовая информация о текущем entry, dimensions после decode и набор полезных EXIF-полей, которые отдаёт libvips.

Fullscreen показывает только область изображения по умолчанию. Панели появляются как overlay поверх изображения при приближении мыши к краю: слева `Files`, справа `Info`, сверху основные actions, снизу status text. Hover-панели не меняют размер viewer и не запускают перерендер изображения; зоны срабатывания по умолчанию — 7% ширины для боковых краёв и 5% высоты для верхнего/нижнего.

Если текущий файл удалён, стал нечитаемым или не декодируется, viewer показывает ошибку для этого entry, но сохраняет коллекцию и навигацию. Ошибки загрузки не попадают в memory cache.

При переключении heavy images текущий raster остаётся на экране до готовности нового decode result; transient `Loading image...` показывается полупрозрачным overlay поверх изображения. При zoom старый raster тоже остаётся на месте, но loading-надпись не показывается, чтобы изображение не дёргалось визуально. Ошибки и пустые состояния по-прежнему очищают изображение и показывают полноценное сообщение.

Для ориентации по коду см. `docs/implementation.md`.

## Управление в MVP

- Wheel — предыдущее/следующее изображение.
- `Ctrl+wheel` — zoom in/out относительно текущего отображаемого масштаба.
- Next/Previous по кнопкам, shortcuts или wheel по умолчанию сбрасывают масштаб и pan в установленный базовый режим `fit` или `actual size`; если листать некуда, текущий image перерендеривается с тем же сбросом.
- Простой клик ЛКМ — временный zoom 2x от текущего отображаемого масштаба с центровкой по точке клика.
- Простой клик ЛКМ после click/rectangle zoom — возврат в исходный режим `fit` или `actual size`.
- Зажатая левая кнопка мыши — pan/hand mode только вне базового `fit`; простой клик ЛКМ работает как zoom/restore.
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

Run:

```sh
./build/insidious
./build/insidious /path/to/image-folder-or-archive
```

The app intentionally does not use recent-file history, persistent thumbnail caches, archive extraction or file logging in the MVP.
