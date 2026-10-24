# qmltestrunner harness. The Kit plugin is linked in, so `import Omaseed.Kit` resolves from
# the module's own resources and needs no import path.
omaseed_add_test(tst_qml
    XDG_DIR "${CMAKE_BINARY_DIR}/tests/qml-env"
    SOURCES tst_qml.cpp
    LIBRARIES Qt6::Qml Qt6::Quick Qt6::QuickTest omaseed::integration omaseed-kitplugin omaseed-viewsplugin omaseed-qmlplugin
    DEFINES QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# The suite runs once per skin (#171, the console skin's design note "Tests"): `tst_qml` under
# Omarchy's, the default (OMASEED_SKIN unset, so the skin choice a test makes is drawn), and
# `tst_qml_console` with OMASEED_SKIN=console pinned. `ctest -L skin-console` (or
# `-R '^tst_qml_console$'`) picks one. Each run has its own folder below, so the two can run
# at once without sharing a state.json.
get_test_property(tst_qml ENVIRONMENT qml_base_env)
foreach(skin omarchy console)
    if(skin STREQUAL "omarchy")
        set(qml_test tst_qml)
        set(qml_env "${CMAKE_BINARY_DIR}/tests/qml-env")
        set(qml_skin_env "")
    else()
        set(qml_test tst_qml_${skin})
        set(qml_env "${CMAKE_BINARY_DIR}/tests/qml-env-${skin}")
        set(qml_skin_env "OMASEED_SKIN=${skin}")
        add_test(NAME ${qml_test} COMMAND tst_qml)
        set_tests_properties(${qml_test} PROPERTIES ENVIRONMENT "${qml_base_env}")
    endif()
    set_tests_properties(${qml_test} PROPERTIES LABELS "qml;skin-${skin}")

    # Theme is the real reader, so the QML tests would otherwise assert against whatever
    # theme and base size this machine happens to run. Point XDG at an empty directory: the
    # reader falls back to the bundled Tokyo Night at base size 12, the spec's reference.
    file(MAKE_DIRECTORY "${qml_env}")
    # The add sheet lists the newest .torrent files in Downloads (App.scanDownloads) each time
    # it opens. Without this, Qt falls back to $HOME/Downloads and the tests would read
    # whatever the person running them saved there. An empty folder of the tests' own; a test
    # that wants files points App.downloadsFolder elsewhere.
    file(MAKE_DIRECTORY "${qml_env}/Downloads")
    file(WRITE "${qml_env}/user-dirs.dirs" "XDG_DOWNLOAD_DIR=\"${qml_env}/Downloads\"\n")
    # The bar widget (#17): the tests never run the desktop's `omarchy` (it would change the
    # bar of whoever runs them), so App.barWidget is unavailable unless a test points it at
    # `true`; the widget it links is the source tree's.
    set_property(TEST ${qml_test} APPEND PROPERTY ENVIRONMENT
        "XDG_STATE_HOME=${qml_env}"
        "XDG_CONFIG_HOME=${qml_env}"
        "OMASEED_OMARCHY=omaseed-tests-have-no-omarchy"
        "OMASEED_OMARCHY_SHELL=omaseed-tests-have-no-omarchy"
        "OMASEED_PLUGIN_DIR=${PROJECT_SOURCE_DIR}/plugin"
        ${qml_skin_env}
    )
    # Switching the widget off keeps its link (BarWidget::remove), so a run would leave one
    # in its folder for the next: every run starts without the tests' plugins folder.
    add_test(NAME ${qml_test}_env_clean
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${qml_env}/omarchy/plugins")
    set_tests_properties(${qml_test}_env_clean PROPERTIES FIXTURES_SETUP ${qml_test}_env)
    set_property(TEST ${qml_test} APPEND PROPERTY FIXTURES_REQUIRED ${qml_test}_env)
endforeach()

# Kit/KeyWatch's alt against the window losing the focus, which offscreen QML tests can't move
# (DECISIONS C24 H10).
omaseed_add_test(tst_keywatch
    SOURCES tst_keywatch.cpp
    LIBRARIES Qt6::Quick omaseed-kit
)
target_include_directories(tst_keywatch PRIVATE "${PROJECT_SOURCE_DIR}/src/ui/qml/Kit")
