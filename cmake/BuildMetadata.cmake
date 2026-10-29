set(VERSION_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DOUTPUT_DIR=${VERSION_OUTPUT_DIR}" -P "${CMAKE_CURRENT_LIST_DIR}/GenerateVersion.cmake"
    COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(insidious_build_metadata
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUTPUT_DIR=${VERSION_OUTPUT_DIR}" -P "${CMAKE_CURRENT_LIST_DIR}/GenerateVersion.cmake"
    BYPRODUCTS "${VERSION_OUTPUT_DIR}/build_version.h"
    VERBATIM
)
add_library(insidious_version INTERFACE)
add_dependencies(insidious_version insidious_build_metadata)
target_include_directories(insidious_version INTERFACE "${VERSION_OUTPUT_DIR}")
