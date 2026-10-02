# qmltestrunner harness. The Kit plugin is linked in, so `import Omaseed.Kit` resolves from
# the module's own resources and needs no import path.
omaseed_add_test(tst_qml
    SOURCES tst_qml.cpp
    LIBRARIES Qt6::Qml Qt6::Quick Qt6::QuickTest omaseed-kitplugin omaseed-viewsplugin omaseed-qmlplugin
    DEFINES QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Theme is the real reader, so the QML tests would otherwise assert against whatever theme
# and base size this machine happens to run. Point XDG at an empty directory: the reader
# falls back to the bundled Tokyo Night at base size 12, which is the spec's reference.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests/qml-env")
# The add sheet lists the newest .torrent files in Downloads (App.scanDownloads) each time it
# opens. Without this, Qt falls back to $HOME/Downloads and the tests would read whatever the
# person running them saved there. An empty folder of the tests' own; a test that wants files
# points App.downloadsFolder elsewhere.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests/qml-env/Downloads")
file(WRITE "${CMAKE_BINARY_DIR}/tests/qml-env/user-dirs.dirs"
    "XDG_DOWNLOAD_DIR=\"${CMAKE_BINARY_DIR}/tests/qml-env/Downloads\"\n")
# The bar widget (#17): the tests never run the desktop's `omarchy` (it would change the bar of
# whoever runs them), so App.barWidget is unavailable unless a test points it at `true`; the
# widget it links is the source tree's.
set_property(TEST tst_qml APPEND PROPERTY ENVIRONMENT
    "XDG_STATE_HOME=${CMAKE_BINARY_DIR}/tests/qml-env"
    "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/tests/qml-env"
    "OMASEED_OMARCHY=omaseed-tests-have-no-omarchy"
    "OMASEED_OMARCHY_SHELL=omaseed-tests-have-no-omarchy"
    "OMASEED_PLUGIN_DIR=${PROJECT_SOURCE_DIR}/plugin"
)
