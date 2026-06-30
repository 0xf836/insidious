cmake_minimum_required(VERSION 3.24)

project(insidious
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPSCPP REQUIRED IMPORTED_TARGET vips-cpp)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)

set(CORE_SOURCES
    src/core/app_settings.cpp
    src/core/app_settings.h
    src/core/archive_entry_stream.cpp
    src/core/archive_entry_stream.h
    src/core/archive_collection_source.cpp
    src/core/archive_collection_source.h
    src/core/decoded_image.h
    src/core/folder_collection_source.cpp
    src/core/folder_collection_source.h
    src/core/image_entry.cpp
    src/core/image_entry.h
    src/core/image_entry_validation.cpp
    src/core/image_entry_validation.h
    src/core/image_formats.cpp
    src/core/image_formats.h
    src/core/image_sort.cpp
    src/core/image_sort.h
    src/core/memory_image_cache.cpp
    src/core/memory_image_cache.h
    src/core/natural_sort.cpp
    src/core/natural_sort.h
    src/core/render_memory_budget.cpp
    src/core/render_memory_budget.h
    src/core/vips_image_decoder.cpp
    src/core/vips_image_decoder.h
    src/core/viewer_controller.cpp
    src/core/viewer_controller.h
)

add_library(insidious_core STATIC ${CORE_SOURCES})
target_include_directories(insidious_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(insidious_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
        PkgConfig::VIPSCPP
        PkgConfig::LIBARCHIVE
)

add_executable(insidious
    src/app/main.cpp
    src/ui/collection_tree_model.cpp
    src/ui/collection_tree_model.h
    src/ui/image_info_panel.cpp
    src/ui/image_info_panel.h
    src/ui/image_list_model.cpp
    src/ui/image_list_model.h
    src/ui/image_view_widget.cpp
    src/ui/image_view_widget.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/quick_jump_dialog.cpp
    src/ui/quick_jump_dialog.h
    src/ui/settings_dialog.cpp
    src/ui/settings_dialog.h
    src/ui/status_line_formatter.cpp
    src/ui/status_line_formatter.h
)
target_link_libraries(insidious
    PRIVATE
        insidious_core
        Qt6::Widgets
)

add_executable(insidious_memory_audit
    tools/memory_audit.cpp
)
target_link_libraries(insidious_memory_audit
    PRIVATE
        insidious_core
)

enable_testing()

add_executable(insidious_core_tests
    tests/core_tests.cpp
)
target_link_libraries(insidious_core_tests
    PRIVATE
        insidious_core
        Qt6::Test
)
add_test(NAME insidious_core_tests COMMAND insidious_core_tests)
set_tests_properties(insidious_core_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(insidious_ui_tests
    tests/status_line_formatter_tests.cpp
    src/ui/status_line_formatter.cpp
    src/ui/status_line_formatter.h
)
target_link_libraries(insidious_ui_tests
    PRIVATE
        insidious_core
        Qt6::Test
        Qt6::Widgets
)
add_test(NAME insidious_ui_tests COMMAND insidious_ui_tests)
set_tests_properties(insidious_ui_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
