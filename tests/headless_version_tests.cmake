file(READ "${HEADER}" header)
string(REGEX MATCH "Version\\[\\] = \"([^\"]+)\"" match "${header}")
set(expected "Insidious ${CMAKE_MATCH_1}\n")
foreach(flag --version -v)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
        "QT_QPA_PLATFORM=unavailable-version-test" "${APP}" "${flag}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0 OR NOT output STREQUAL expected OR NOT error STREQUAL "")
        message(FATAL_ERROR "Headless ${flag} failed: ${result}: ${output} ${error}")
    endif()
endforeach()
