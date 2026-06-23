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

Сначала делается маленькая, стабильная версия: открыть изображение, открыть папку, навигация, масштабирование, fullscreen, базовый drag'n'drop. Архивы, RAW, JPEG XL, глубокая настройка UI и сложные панели добавляются после стабилизации базового viewer pipeline.

## Текущая реализация

Проект сейчас реализован как C++20 / Qt6 Widgets приложение с libvips/vips-cpp для декодирования изображений.

- `src/app/` — запуск процесса, инициализация libvips, создание `QApplication`, обработка optional path argument.
- `src/ui/` — Qt Widgets слой: главное окно, actions/shortcuts, drag'n'drop, statusbar, fullscreen, левая панель списка файлов, widget отрисовки изображения.
- `src/core/` — логика без виджетов: `ImageEntry`, фильтр форматов MVP, natural sort, сканирование папки, состояние viewer, memory-only cache, libvips decoder.
- `tests/` — Qt Test тесты для core-логики.

Главный поток открытия изображения: UI принимает путь, core создаёт одиночную коллекцию или сканирует папку, `ViewerController` выбирает текущий entry, `VipsImageDecoder` асинхронно строит display-sized `QImage`, `MainWindow` обновляет `ImageViewWidget` и statusbar.

Левая панель `Files` показывает текущую коллекцию без thumbnails. Список синхронизируется с `ViewerController`: next/previous/first/last обновляют выделение, а выбор строки мышью или клавиатурой переключает текущее изображение через существующий viewer pipeline.

Если текущий файл удалён, стал нечитаемым или не декодируется, viewer показывает ошибку для этого entry, но сохраняет коллекцию и навигацию. Ошибки загрузки не попадают в memory cache.

При переключении или zoom heavy images текущий raster остаётся на экране до готовности нового decode result; transient `Loading image...` показывается полупрозрачным overlay поверх изображения. Ошибки и пустые состояния по-прежнему очищают изображение и показывают полноценное сообщение.

Для ориентации по коду см. `docs/implementation.md`.

## Управление в MVP

- Wheel — предыдущее/следующее изображение.
- `Ctrl+wheel` — zoom in/out.
- Next/Previous по кнопкам, shortcuts или wheel по умолчанию сбрасывают масштаб и pan в установленный базовый режим `fit` или `actual size`; если листать некуда, текущий image перерендеривается с тем же сбросом.
- Простой клик ЛКМ — временный zoom 200% с центровкой по точке клика.
- Простой клик ЛКМ после click/rectangle zoom — возврат в исходный режим `fit` или `actual size`.
- Зажатая левая кнопка мыши — pan/hand mode только вне базового `fit`; простой клик ЛКМ работает как zoom/restore.
- Зажатая правая кнопка мыши — прямоугольник zoom-to-selection.
- Double click — fullscreen; temporary click zoom is suppressed for double clicks.

## Build

MVP implementation uses C++20, Qt6 Widgets and libvips/vips-cpp.

Arch-like systems:

```sh
sudo pacman -S qt6-base libvips cmake ninja pkgconf
```

Debian/Ubuntu-like systems:

```sh
sudo apt install qt6-base-dev libvips-dev cmake ninja-build pkg-config
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
./build/insidious /path/to/image-or-folder
```

The app intentionally does not use recent-file history, persistent thumbnail caches, archive extraction or file logging in the MVP.
