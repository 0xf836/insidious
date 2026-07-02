# UI specification

## Общий ориентир

Интерфейс ориентируется на классический быстрый image viewer: toolbar, скрытое главное меню с доступом через `Alt` или toolbar menu button, центральная область просмотра, левая навигационная панель и нижняя статусная строка. Примерный визуальный ориентир — FastStone MaxView-like layout: дерево/список слева, изображение по центру, toolbar сверху, statusbar снизу.

## Главное окно

Главное окно состоит из:

- центральной области просмотра изображения;
- верхней панели действий и главного меню, доступного через `Alt` или toolbar menu button;
- нижней статусной строки;
- левой панели навигации;
- опциональной правой панели расширенной информации.

## Центральная область просмотра

Центральная область должна:

- показывать текущее изображение;
- использовать нейтральный тёмный фон;
- поддерживать `fit to window`;
- поддерживать `actual size`;
- поддерживать zoom;
- поддерживать pan при увеличении;
- давать выбирать quality preset для scaling/filtering без смешивания raster cache между разными preset values;
- по умолчанию экономить память region-based rendering для zoom/actual size, но давать отключить это в Preferences ради более плавного pan;
- по умолчанию учитывать embedded ICC profiles и конвертировать rendered raster в sRGB, с возможностью отключить это в Preferences;
- не мерцать при переключении изображений;
- не блокировать весь интерфейс во время загрузки следующего изображения.
- при transient loading во время переключения оставлять предыдущий raster на экране и показывать ненавязчивый полупрозрачный overlay вместо чёрного экрана; при zoom не показывать loading-надпись и не двигать старый raster до готовности нового.
- если zoom-запрос слишком большой для render budget, оставлять предыдущий raster, zoom и pan на месте и показывать компактный overlay `Too large to render`.
- пока новый raster еще рендерится, не наслаивать дополнительные zoom/view-transform команды; навигация между изображениями при этом остается доступной.

## Верхняя панель

Верхняя панель использует компактные кнопки поверх полноценных `QAction`, чтобы меню, shortcuts и enabled state оставались единым источником поведения. Кнопки сгруппированы логически:

- source: open file, open folder;
- browse: previous/next image, previous/next folder, first/last, quick jump;
- files: copy original image;
- view: zoom, fit, actual size, rotate;
- app menu: open file, open folder, fullscreen/exit fullscreen, EXIF autorotate, runtime sorting, Preferences, Quit.

Normal toolbar использует короткие labels и прижимает app menu button к правому краю. Fullscreen top overlay использует еще более компактные labels/glyphs; его app menu button остаётся внутри основного блока кнопок, чтобы не перекрываться правой edge panel, и сама панель всегда остаётся тёмной полупрозрачной с читаемым светлым текстом.

Главное меню (`File`, `Edit`, `View`, `Navigate`) в normal mode скрыто по умолчанию. Нажатие `Alt` временно показывает меню и переводит туда фокус; после выбора menu action, закрытия popup-меню или ухода фокуса меню снова скрывается. В normal и fullscreen toolbar также есть компактная menu button с частыми app-level командами: open file/folder, fullscreen, EXIF orientation, sorting, Preferences и Quit. В fullscreen normal menu bar не показывается, вместо него используются edge overlay panels.

## Нижняя статусная строка

Статусная строка показывает:

- разрешение изображения и мегапиксели: `XXXX x YYYY = F.F MP`;
- размер файла;
- дату файла из metadata источника с префиксом `modified:` в формате `YYYY.M.D HH:MM:SS`;
- дату съёмки из EXIF с префиксом `taken:` в формате `YYYY.M.D HH:MM:SS`;
- имя файла или относительный путь внутри коллекции;
- масштаб;
- номер текущего изображения и общее количество;

Все поля показываются как короткие приглушённые labels и значения, разделённые вертикальными separators, чтобы строка читалась как набор сканируемых блоков.

Пример:

```text
image: 3412 x 2188 = 7.5 MP | size: 1.8 MB | modified: 2026.6.29 12:34:00 | taken: 2024.5.4 10:20:30 | name: folder/image.jpg | zoom: 42% | pos: 12 / 240
```

Fullscreen bottom overlay использует тот же formatter и тот же состав полей, что обычная статусная строка. Pinned fullscreen info line — отдельный нескрываемый overlay в левом верхнем углу области изображения. Он включён по умолчанию, не имеет labels и использует формат `name (resolution | filesize) [index/count] zoom`. Если edge panels перекрывают эту строку, дополнительный layout avoidance не выполняется.
EXIF date в status/fullscreen line показывается только как `YYYY.M.D HH:MM:SS`, без timezone/extension suffixes.

## Левая панель

Левая панель имеет два режима:

1. Навигация по папкам/архиву.
2. Миниатюры.

Текущая реализация навигационного режима — dock-панель `Files`, вертикально разделенная на две части:

- сверху дерево папок текущего source;
- снизу список изображений выбранной папки.

Выбранная папка становится активной коллекцией viewer: quick jump, status index и выделение файла работают внутри этой папки. `next/previous` идут по изображениям выбранной папки, а при достижении границы переходят к следующей/предыдущей подходящей папке в порядке дерева. `first/last` остаются действиями внутри активной папки. Splitter между деревом и списком можно двигать; при включённой настройке `Remember window layout` его состояние сохраняется.

Для filesystem folders подпапки загружаются лениво при выборе или раскрытии ветки. Это позволяет открыть папку без полного рекурсивного обхода дерева. Для архивов приложение асинхронно перечисляет headers, строит дерево virtual paths и per-folder file lists без распаковки на диск; image data читается только при decode выбранного файла. Если архив требует пароль, приложение запрашивает его при открытии и повторяет запрос при неверном пароле до успешного ввода или отмены.

Для multi-source открытия дерево показывает синтетический root. Несколько выбранных изображений становятся одним плоским временным списком и сначала сохраняют порядок выбора. Несколько папок или архивов показываются как top-level children в порядке выбора; внутри каждой папки сохраняется lazy loading, а внутри каждого архива сохраняется virtual folder tree. Multi-open через Open File принимает изображения или архивы, multi-drop также принимает папки. Наборы должны быть однородными: изображения, папки или архивы; смешанные наборы отклоняются без частичного открытия.

Если выбранная папка не содержит direct images, нижний список пустой, а viewer показывает empty state. Изображения из подпапок не подмешиваются автоматически в выбранную папку.

Кнопка `Sorting` в toolbar управляет runtime-сортировкой изображений активной папки: name, type, size или date, для каждого режима asc/desc. По умолчанию при запуске используется natural sort по name ascending. Выбранный режим живёт только в текущем процессе и применяется также к новым папкам/архивам, но не сохраняется в Preferences. Сортировка меняет только порядок files/images; порядок папок и top-level multi-source roots в дереве не меняется. Entries без даты располагаются после entries с валидной датой в обоих направлениях.

## Быстрый переход

`Navigate -> Quick Jump` (`Ctrl+F`) открывает небольшой модальный диалог поиска по имени файла в текущей коллекции. Поиск использует case-insensitive contains matching по отображаемому имени файла. Подтверждение выбранной строки переключает изображение через тот же pipeline, что и выбор строки в панели `Files`. В fullscreen диалог открывается поверх viewer без выхода из fullscreen.

## Clipboard

`Edit -> Copy Original Image` (`Ctrl+C`) копирует в clipboard исходные bytes текущего image с MIME исходного формата и добавляет совместимое clipboard image data, декодированное из этих же исходных bytes. Команда не копирует rendered raster: zoom, fit, rotate, EXIF autorotate и display scaling не меняют clipboard payload. Для archive entries bytes читаются из архива в память без распаковки на диск.

## Правая панель

Правая панель опциональна. Текущая реализация — dock-панель `Info`, скрытая по умолчанию и включаемая через меню `View`. Она группирует данные в компактные секции `Info` и `EXIF`. Базовая секция показывает:

- имя файла;
- полный путь;
- формат;
- размер файла;
- dimensions после decode;
- статус color profile;
- дата изменения;
- компактный набор полезных EXIF-полей, которые отдаёт libvips.

Позже панель может показывать расширенную информацию:

- технические параметры изображения;
- расширенные ICC/profile details beyond the current profile status.

## Fullscreen

Fullscreen режим максимально чистый. По умолчанию видна только область изображения, без menu bar, toolbar, statusbar и dock-панелей. Остальной UI появляется как overlay поверх изображения и не должен менять размер viewer, zoom, pan или запускать новый render/decode.

Edge behavior:

- левый край показывает overlay `Files`;
- правый край показывает overlay `Info`;
- верхний край показывает overlay с основными actions;
- нижний край показывает overlay со status text;
- левый верхний угол показывает pinned info line, если она включена в настройках;
- боковые зоны имеют приоритет в углах;
- панели появляются и скрываются сразу, без animation;
- `Ctrl+F` в fullscreen открывает quick jump поверх изображения и временно скрывает edge overlays;
- `Esc` выходит из fullscreen.

Trigger zones:

- боковые панели: 7% ширины окна по умолчанию;
- верхняя и нижняя панели: 5% высоты окна по умолчанию;
- значения настраиваются в `Edit -> Preferences -> Fullscreen`.

Pinned fullscreen info line:

- формат: `name (resolution | filesize) [index/count] zoom`;
- default: enabled, `DejaVu Sans`, `10 pt`, `#00ff00`, shadow for readability on dark background and images;
- в настройках можно включить/выключить строку, выбрать font family, font size и color.

## Настройки

`Edit -> Preferences` открывает базовое окно настроек. Оно состоит из вкладок:

- `Viewer` — секции `View Defaults`, `Zoom and Rendering`, `Wheel Actions`: default zoom mode, поведение при переключении изображений (`reset zoom and pan` или `preserve current view`), EXIF autorotate, режим ЛКМ zoom (`click to zoom / restore` или `hold to zoom, drag to pan`), multiplier для temporary click zoom, общий zoom step, scaling quality preset, embedded color profile conversion, region-based rendering и действия для wheel / `Ctrl+wheel` / `Shift+wheel`.
- `Shortcuts` — секция `Assignments` для core viewer shortcuts: открыть файл/папку, навигация по изображениям и папкам, quick jump, Preferences, zoom/fit/actual size, rotate, EXIF orientation и fullscreen. Команду можно назначить на keyboard shortcut и/или mouse button shortcut с optional modifiers.
- `Fullscreen` — секции `Edge Overlays` и `Pinned Info Line`: размер edge trigger zones для боковых и верхней/нижней overlay-панелей, включение и presentation settings для pinned fullscreen info line.
- `Interface` — секции `Appearance` и `Layout`: light/dark theme для normal mode, запоминание layout окна, dock-панелей и splitter sizes. Fullscreen chrome остаётся темным без отдельного выбора темы.

Страницы Preferences прокручиваются при нехватке высоты, чтобы секции не сжимались ниже нормальной высоты контролов.

EXIF autorotate также доступен как быстрый checkable action в меню `View`, обычной верхней панели и fullscreen top overlay. Переключение сразу сохраняет настройку и перерендеривает текущий image, если он открыт.

Shortcut editor разрешает очистить shortcut у команды и сбросить команду к default. Apply/OK блокирует дублирующиеся non-empty keyboard или mouse shortcuts в редактируемом наборе. В текущем срезе не редактируются `Quit`, dock toggles `Files`/`Info`, profiles/import-export и shortcut scopes.

Настройки сохраняются через стандартный механизм Qt `QSettings`. Они не должны содержать историю открытых файлов, пути к просмотренным источникам, пароли архивов, thumbnails или decoded cache. Shortcut settings содержат только stable command ids и portable input strings, например `Ctrl+O` или `MouseBack`.

## Мышь

Базовое поведение:

- колесо мыши: следующее/предыдущее изображение;
- `Ctrl + wheel`: zoom in / zoom out относительно текущего отображаемого масштаба, включая текущий `fit` scale, с опорой на позицию курсора;
- `Shift + wheel`: no action по умолчанию;
- действия wheel / `Ctrl+wheel` / `Shift+wheel` можно настроить как navigation, zoom или no action;
- mouse button shortcut, назначенный в `Preferences -> Shortcuts`, срабатывает на press и имеет приоритет над встроенным mouse gesture для той же кнопки; по умолчанию `MouseBack` переходит к предыдущей подходящей папке source tree и выбирает первый файл с учетом текущей сортировки, а `MouseForward` делает то же для следующей папки;
- toolbar, shortcuts и wheel zoom используют общий настраиваемый zoom step, по умолчанию 25%; toolbar и shortcuts масштабируют вокруг центра viewport;
- next/previous через toolbar, shortcuts или wheel по умолчанию сбрасывает zoom и pan в установленный базовый режим `fit` или `actual size`; в настройках можно переключить это на сохранение текущего view. Если в активной папке достигнута граница, но дальше по дереву есть подходящая папка, viewer переходит к ней; если переходить некуда, текущий entry перерендеривается с тем же сбросом;
- rotate left/right доступен из `View`, обычного toolbar, fullscreen top overlay и shortcuts; поворот является view-only, не меняет исходный файл и сбрасывается при переходе на другое изображение;
- ЛКМ по умолчанию: простой клик включает временный zoom 2x от текущего отображаемого масштаба с центровкой по точке клика; multiplier настраивается;
- ЛКМ по умолчанию после click/rectangle zoom: простой клик возвращает исходный режим `fit` или `actual size`;
- magnifier cursor показывается в idle-состоянии режимов ЛКМ zoom только когда действие доступно: изображение уже декодировано, render не ожидается, hold zoom не активен;
- ЛКМ по умолчанию при удержании: pan/hand mode только вне базового `fit`;
- в любом non-fit view, где отображаемое изображение больше viewport: зажатая средняя кнопка мыши или `Ctrl+зажатая ЛКМ` двигают текущий view без auto zoom;
- альтернативный режим ЛКМ в настройках: зажатие сразу включает временный zoom всего изображения, движение сдвигает увеличенный raster, отпускание возвращает исходный view;
- зажатая правая кнопка: прямоугольник zoom-to-selection;
- drag'n'drop в окно: открыть соответствующий одиночный источник или однородный multi-source набор; смешанные наборы не открываются.
- double click - переход в Fullscreen/выход из него; временный zoom по простому клику при double click не срабатывает

## Клавиатура

Базовые shortcuts по умолчанию:

- open file: `Ctrl+O`;
- open folder: `Ctrl+Shift+O`;
- previous image: `Left`, `PageUp`;
- next image: `Right`, `PageDown`;
- previous/next folder: `MouseBack`, `MouseForward`;
- first/last image: `Home`, `End`;
- quick jump: `Ctrl+F`;
- copy original image: `Ctrl+C`;
- preferences: toolkit default Preferences shortcut;
- zoom in/out: toolkit default Zoom In / Zoom Out shortcuts;
- fit to window / actual size: `0`, `1`;
- rotate left/right: `[`, `]`;
- fullscreen: `F11`;
- exit fullscreen: `Esc`.

Core viewer shortcuts кроме `Esc` редактируются в `Edit -> Preferences -> Shortcuts`.
