include("${CMAKE_CURRENT_LIST_DIR}/Version.cmake")
insidious_build_identity("${SOURCE_DIR}" INSIDIOUS_BUILD_VERSION INSIDIOUS_BUILD_COMMIT)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/build_version.h.in" "${OUTPUT_DIR}/build_version.h" @ONLY)
