# Linux Image Viewer

Рабочее название проекта: **Linux Image Viewer**.

Цель проекта — нативное Linux-приложение для быстрого просмотра изображений, папок и архивов с изображениями через GUI. Программа должна быть быстрой, предсказуемой, аккуратной к памяти и не оставлять следов просмотра.

## Основные документы

- `docs/product.md` — назначение продукта и основные сценарии.
- `docs/requirements.md` — функциональные и нефункциональные требования.
- `docs/mvp.md` — границы первой рабочей версии.
- `docs/ui.md` — структура интерфейса и поведение UI.
- `docs/architecture.md` — предполагаемые модули и границы ответственности.
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
