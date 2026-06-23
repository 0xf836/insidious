# Architecture

## Главный принцип

GUI не должен знать, откуда физически пришло изображение: из папки, списка файлов, архива или будущего источника. Для UI всё должно выглядеть как коллекция изображений с текущим индексом и операциями навигации.

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
- `SingleFileWithFolderContextSource`.

Каждый source должен уметь вернуть список image entries и открыть конкретный entry для чтения.

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

Текущая базовая реализация использует libarchive для ZIP/7z/RAR без распаковки на диск. Password prompt и расширенная работа с encrypted archives остаются следующим этапом.

### Cache

Memory-only cache для:

- текущего изображения;
- предыдущего изображения;
- следующего изображения;
- возможно, нескольких thumbnails после открытия панели миниатюр.

Persistent cache запрещён текущими privacy requirements.

### Settings

Хранит настройки приложения, но не хранит историю открытых файлов/папок/архивов.

- default zoom mode;
- default sort mode;
- UI visibility preferences;
- fullscreen info line configuration;
- hotkeys;
- rendering preferences.

## Поток открытия файла

1. Пользователь открывает файл.
2. App определяет тип источника.
3. Если это обычное изображение, создаётся collection source с контекстом родительской папки.
4. Folder scanner строит список изображений.
5. Viewer state устанавливает текущий entry.
6. Image loader декодирует текущее изображение.
7. Cache запускает prefetch previous/next.
8. UI обновляет изображение и statusbar.

## Поток открытия архива

1. Пользователь открывает архив.
2. Archive Adapter читает список entries.
3. Неизображения отбрасываются.
4. Если архив зашифрован, пользователь вводит пароль.
5. Создаётся archive collection.
6. Viewer работает с ней так же, как с папкой.

Текущая реализация покрывает этот поток для обычных ZIP/7z/RAR архивов без пароля.
