# One helper so every test agrees on linkage, output directory and a headless environment.
# `LIVE` marks a test that talks to the :8081/:8082 test servers: those share one ctest
# resource lock so `ctest -j` never runs two of them at once (they add, count and delete
# torrents on the same playground and used to fail each other under parallel runs).
function(omaseed_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "LIVE" "XDG_DIR" "SOURCES;LIBRARIES;DEFINES")

    # OffscreenDefault.cpp: a test run outside ctest still never opens a window
    # (OMASEED_TEST_PLATFORM names another platform on purpose), and never reads or writes
    # the real XDG homes: XDG_DIR is where its config and state go then (default
    # build/tests/direct-env).
    qt_add_executable(${name} ${arg_SOURCES} "${PROJECT_SOURCE_DIR}/tests/OffscreenDefault.cpp")
    if(NOT arg_XDG_DIR)
        set(arg_XDG_DIR "${CMAKE_BINARY_DIR}/tests/direct-env")
    endif()
    file(MAKE_DIRECTORY "${arg_XDG_DIR}")
    target_compile_definitions(${name} PRIVATE
        OMASEED_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
        OMASEED_TEST_XDG_DIR="${arg_XDG_DIR}"
    )
    target_link_libraries(${name} PRIVATE omaseed::warnings Qt6::Test ${arg_LIBRARIES})
    # tests/LiveServer.hpp: every live test's server URL, behind the :8080 guard.
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    if(arg_DEFINES)
        target_compile_definitions(${name} PRIVATE ${arg_DEFINES})
    endif()
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_test(NAME ${name} COMMAND ${name})
    # XDG_DATA_HOME: an App that start()s opens its seed ledger and makes automatic backups
    # (#31) under the data home; a test must never write into the real one.
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=qt.qml.*=false;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tests/data-home"
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
add_subdirectory(plugin)

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

    # scripts/dev/home-guard.sh: every dev script refuses :8080 before it does anything.
    # Starts no server and sends no request.
    add_test(
        NAME dev_home_guard
        COMMAND bash "${CMAKE_SOURCE_DIR}/tests/dev/test_home_guard.sh"
    )
    set_tests_properties(dev_home_guard PROPERTIES TIMEOUT 60)

    # test-server.sh --strict makes, keeps and rotates its own login and API key, on a
    # throwaway qbittorrent-nox (free port, scratch profile and dev.env): the :8081/:8082
    # servers and the developer's dev.env are never touched. Skips without qbittorrent-nox.
    add_test(
        NAME dev_strict_credentials
        COMMAND bash "${CMAKE_SOURCE_DIR}/tests/dev/test_strict_credentials.sh"
    )
    set_tests_properties(dev_strict_credentials PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)

    # data/hooks/theme-set.d/omaseed on a private bus: it must never D-Bus-activate omaseed
    # or hold omarchy-theme-set up.
    add_test(
        NAME theme_set_hook
        COMMAND bash "${CMAKE_SOURCE_DIR}/tests/dev/test_theme_hook.sh"
    )
    set_tests_properties(theme_set_hook PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

    # install-omarchy-extras.sh --menu/--uninstall and install-desktop.sh, with HOME in a
    # temporary directory: the user's own ~/.config is never touched.
    add_test(
        NAME dev_install_extras
        COMMAND bash "${CMAKE_SOURCE_DIR}/tests/dev/test_install_extras.sh"
    )
    set_tests_properties(dev_install_extras PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

    # Every tracked shell script is shellcheck-clean (settings in .shellcheckrc). Skips
    # when shellcheck is not installed.
    add_test(
        NAME shellcheck
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/dev/shellcheck.sh"
    )
    set_tests_properties(shellcheck PROPERTIES LABELS "lint" TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()
