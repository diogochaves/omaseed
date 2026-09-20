# One helper so every test agrees on linkage, output directory and a headless environment.
# `LIVE` marks a test that talks to the :8081/:8082 test servers: those share one ctest
# resource lock so `ctest -j` never runs two of them at once (they add, count and delete
# torrents on the same playground and used to fail each other under parallel runs).
function(omaseed_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "LIVE" "" "SOURCES;LIBRARIES;DEFINES")

    qt_add_executable(${name} ${arg_SOURCES})
    target_link_libraries(${name} PRIVATE omaseed::warnings Qt6::Test ${arg_LIBRARIES})
    if(arg_DEFINES)
        target_compile_definitions(${name} PRIVATE ${arg_DEFINES})
    endif()
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=qt.qml.*=false"
    )
    if(arg_LIVE)
        set_tests_properties(${name} PROPERTIES RESOURCE_LOCK test-servers LABELS "live")
    endif()
endfunction()

add_subdirectory(app)
add_subdirectory(core)
add_subdirectory(integration)
add_subdirectory(manual)
add_subdirectory(qml)

if(NOT WIN32)
    # scripts/release.sh, exercised end to end in a throwaway git clone. See
    # tests/release/test_release.sh; it never touches this checkout's git state or network.
    add_test(
        NAME release
        COMMAND bash "${CMAKE_SOURCE_DIR}/tests/release/test_release.sh"
    )
    set_tests_properties(release PROPERTIES
        LABELS "release"
        TIMEOUT 120
    )
endif()
