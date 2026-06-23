# Roadmap

## Phase 0 — Specification and skeleton

- Разложить спецификацию по `docs/`.
- Создать `AGENTS.md` и `PLANS.md`.
- Выбрать стек.
- Создать минимальный проект.
- Запустить пустое окно.

## Phase 1 — Basic file viewer

- Открытие одного изображения.
- Отображение в главном окне.
- `fit to window`.
- `actual size`.
- Zoom in/out.
- Pan увеличенного изображения.
- Базовая статусная строка.

## Phase 2 — Folder context and navigation

- Открытие папки.
- Если открыт файл — родительская папка становится контекстом.
- Natural sort по имени.
- Next/previous.
- First/last.
- Номер текущего изображения и общее количество.
- Обработка пустой папки.

## Phase 3 — Input and fullscreen

- Keyboard shortcuts.
- Mouse wheel navigation.
- Ctrl+wheel zoom behavior.
- Left-click default zoom / restore behavior.
- Left-button pan mode.
- Right-button rectangle zoom.
- Fullscreen.
- Drag'n'drop файла и папки.

## Phase 4 — Robustness and performance

- Image loading outside UI thread.
- Memory cache current/previous/next.
- Prefetch соседних изображений.
- Обработка corrupted/unsupported files без сброса коллекции.
- Поведение при удалении файла во время просмотра без сброса коллекции.
- Проверка больших папок.

## Phase 5 — Metadata and side panels

- Базовая информация об изображении.
- EXIF reader.
- Правая панель информации.
- Левая панель списка файлов.
- Быстрый переход по имени файла.

## Phase 6 — Thumbnails

- Панель миниатюр.
- Lazy thumbnail generation.
- Memory-only thumbnail cache.
- Отмена генерации thumbnails при закрытии панели.

## Phase 7 — Archives

- Basic ZIP/7z/RAR archive collections through libarchive.
- Password prompt.
- Archive as collection source.
- No disk extraction.
- Error handling for corrupted/encrypted archives.

## Phase 8 — Advanced formats and rendering

- AVIF.
- JPEG XL.
- RAW.
- ICC/color profile handling.
- Rendering quality settings.
- Scaling filters / antialiasing settings.

## Phase 9 — Settings and polish

- Basic settings dialog for viewer defaults, fullscreen trigger zones, image-switch policy and layout persistence. Done.
- Hotkey customization.
- Fullscreen info line customization.
- Statusbar customization.
- Default sort mode.
- Advanced rendering and status presentation settings.
- Packaging for Linux.

## Phase 10 - Fixes
- Сделать модальное окно поиска в fullscreen меньше
- В модальном окне поиска в fullscreen-режиме при фокусе ввода текста кнопки вверх-вниз должны изменять выбранное в результатах поиска
- zoom на ctrl+wheel на курсор, а не на центр изображения

## Phase 11 - Some features
- Добавить возможность поворота изображения кнопками по/против часовой стрелки на 90 градусов
- Включить/отключить автоповорот по EXIF в настройках
- Копировать изображение буфер обмена

# Phase 12 - Optimization
- Почекать корректность выделения и очистки памяти
- Почекать lazy-load, оптимизировать, если есть необходимость

# Phase 13 - Improvements, refactoring
