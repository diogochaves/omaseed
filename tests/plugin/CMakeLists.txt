# The companion bar widget's Format.js, Status.js (#16) and IpcOwner.js. Its QML needs the
# Omarchy shell and is checked in an omabox instead.
omaseed_add_test(tst_plugin
    SOURCES tst_plugin.cpp
    LIBRARIES Qt6::Qml Qt6::Quick Qt6::QuickTest
    DEFINES QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# `omarchy plugin validate plugin/`: the manifest, entry points and paths the shell enforces.
# Only reads files. Skips where Omarchy is not installed (the CI container).
if(NOT WIN32)
    add_test(
        NAME plugin_validate
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/validate.sh" "${PROJECT_SOURCE_DIR}/plugin"
    )
    set_tests_properties(plugin_validate PROPERTIES LABELS "lint" TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()
