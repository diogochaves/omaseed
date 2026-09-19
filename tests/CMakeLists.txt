# One helper so every test agrees on linkage, output directory and a headless environment.
function(omaseed_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES;DEFINES")

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
endfunction()

add_subdirectory(core)
add_subdirectory(integration)
add_subdirectory(qml)
