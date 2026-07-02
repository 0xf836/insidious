# Architecture

## Главный принцип

GUI не должен знать, откуда физически пришло изображение: из папки, списка файлов, архива или будущего источника. Для viewer всё должно выглядеть как коллекция изображений с текущим индексом и операциями навигации. Для панели `Files` source дополнительно выглядит как дерево папок, где выбранная папка становится активной viewer collection.

## Предлагаемые слои

### App / Bootstrap

Отвечает за запуск приложения, загрузку настроек, создание главного окна и связывание зависимостей.

### UI Layer

Отвечает за окна, меню, toolbar, statusbar, panels, drag'n'drop и пользовательские события.

UI Layer не должен напрямую читать архивы, декодировать изображения или сканировать папки.

### Viewer Controller / Viewer State

Отвечает за:

- текущую коллекцию;
- текущий индекс;
- переход next/previous/first/last;
- текущий zoom mode;
- текущий pan offset;
- rotation state;
- команды просмотра.

### Collection Source

Единый интерфейс для источников изображений.

Возможные реализации:

- `FolderCollectionSource`;
- `FileListCollectionSource`;
- `ArchiveCollectionSource`;
- synthetic multi-source root for однородные наборы выбранных изображений, папок или архивов;
- `SingleFileWithFolderContextSource`.

Каждый source должен уметь вернуть дерево папок, список direct image entries для выбранной папки и открыть конкретный entry для чтения. Filesystem source должен поддерживать lazy loading подпапок; archive source строит дерево по headers без распаковки на диск.

### Image Entry

Описывает один элемент коллекции:

- display name;
- source path или virtual path;
- file size, если доступен;
- modified time, если доступен;
- format hint;
- source-specific identifier.

### Image Loader / Decoder

Отвечает за декодирование изображения в формат, пригодный для отображения.

Требования:

- не блокировать UI;
- корректно сообщать об ошибках;
- учитывать EXIF orientation, если включено;
- не держать лишние decoded images в памяти.

### Metadata Reader

Отвечает за базовые metadata и EXIF. Не должен блокировать UI.

### Archive Adapter

Отвечает за чтение ZIP/RAR/7Z как коллекции изображений.

Требования:

- без распаковки архива на диск в нормальном режиме;
- поддержка пароля;
- фильтрация неизображений;
- обработка ошибок чтения.

Текущая базовая реализация использует libarchive для ZIP/7z/RAR без распаковки на диск. Password prompt реализован; пароль хранится только в памяти активного archive source. Сохранение паролей и расширенная работа с encrypted archives остаются следующим этапом.

### Cache

Memory-only cache для:

- текущего изображения;
- предыдущего изображения;
- следующего изображения;
- возможно, нескольких thumbnails после открытия панели миниатюр.

Persistent cache запрещён текущими privacy requirements.

Render-memory budget считается отдельной core-логикой для output raster decode request: полный display-sized raster в `fit`, а в non-fit zoom/actual-size по умолчанию видимый region плюс запас. Scaling quality, color management и device pixel ratio являются частью decode request и cache key, потому что выбранный libvips kernel, ICC conversion mode и DPR меняют output raster. Если пользователь отключает region-based rendering, non-fit request снова проверяет полный scaled raster тем же budget guard. Текущая оценка не является total peak memory budget: поверх неё остаются libvips working/cache memory, source-reader buffering, displayed raster и decoded cache.

Total peak memory проверяется отдельным developer-only audit tooling (`insidious_memory_audit` + `tools/memory_audit.py`) и документируется в `docs/memory-audit.md`. Этот audit не является runtime guard и не пишет persistent data.

### Settings

Хранит настройки приложения, но не хранит историю открытых файлов/папок/архивов.

Текущая реализация:

- `src/core/app_settings.*` — schema/defaults/read/write/validation для `QSettings`;
- `src/ui/settings_dialog.*` — Qt dialog для редактирования настроек;
- `MainWindow` применяет настройки viewer/fullscreen/layout и сохраняет их при Apply/OK и закрытии.

Сейчас сохраняются:

- default zoom mode;
- reset/preserve view при переключении изображений;
- left-button zoom behavior;
- EXIF autorotate;
- click zoom multiplier;
- wheel / Ctrl+wheel / Shift+wheel actions;
- shared zoom step percent;
- scaling quality preset;
- embedded color profile conversion;
- core viewer keyboard/mouse shortcuts;
- fullscreen edge trigger ratios;
- pinned fullscreen info line visibility/font/size/color;
- window/dock/splitter layout, если включено.

Позже сюда можно добавить default sort mode, advanced shortcut preferences и color-management rendering preferences. Даже будущие настройки не должны хранить историю просмотра, recent paths, archive passwords, thumbnails или decoded cache.

## Поток открытия файла

1. Пользователь открывает файл.
2. App определяет тип источника.
3. Если это обычное изображение, создаётся collection source с контекстом родительской папки.
4. Folder scanner загружает корневую папку и прямые подпапки.
5. Viewer state устанавливает текущий entry в активной папке.
6. Image loader декодирует текущее изображение.
7. Cache запускает fit-sized prefetch previous/next.
8. UI обновляет изображение и statusbar.

## Поток открытия архива

1. Пользователь открывает архив.
2. Archive Adapter асинхронно перечисляет headers.
3. Неизображения отбрасываются, а изображения группируются по virtual folders.
4. Если архив зашифрован, пользователь вводит пароль.
5. Создаётся archive source tree.
6. Viewer работает с выбранной папкой архива так же, как с выбранной папкой filesystem source.

Текущая реализация покрывает этот поток для ZIP/7z/RAR архивов, включая архивы с паролем без сохранения пароля. При открытии нескольких архивов используется синтетический root с каждым архивом как top-level node; пароль спрашивается один раз и хранится только в памяти текущего source для соответствующих archive paths.

## Поток открытия нескольких источников

1. UI классифицирует выбранные/drop paths как изображения, папки или архивы.
2. Если набор смешанный или содержит неподдерживаемый path, открытие отменяется с ошибкой.
3. Core строит синтетический source tree:
   - несколько изображений — один flat root с временной коллекцией;
   - несколько папок — top-level folder roots в порядке выбора, с lazy loading подпапок;
   - несколько архивов — top-level archive roots в порядке выбора, с virtual folder tree внутри каждого архива.
4. Viewer работает с выбранной папкой synthetic tree так же, как с обычной папкой или архивом.
