# The companion bar widget's Format.js, Status.js (#16) and IpcOwner.js, and its rows on a
# stand-in for the shell's kit (kit/, #80). The rest of its QML needs the Omarchy shell and is
# checked in an omabox instead.
omaseed_add_test(tst_plugin
    SOURCES tst_plugin.cpp
    LIBRARIES Qt6::Qml Qt6::Quick Qt6::QuickTest
    DEFINES QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# The same, on the kit omacom/omarchy PR #13212 makes: a row still presses once, and a kit
# control that presses itself is not pressed twice.
add_test(NAME tst_plugin_pr13212 COMMAND tst_plugin)
get_test_property(tst_plugin ENVIRONMENT plugin_test_env)
set_tests_properties(tst_plugin_pr13212 PROPERTIES
    ENVIRONMENT "${plugin_test_env};OMASEED_PLUGIN_KIT=pr-13212"
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
