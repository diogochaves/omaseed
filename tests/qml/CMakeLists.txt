# qmltestrunner harness. The Kit plugin is linked in, so `import Omaseed.Kit` resolves from
# the module's own resources and needs no import path.
omaseed_add_test(tst_qml
    SOURCES tst_qml.cpp
    LIBRARIES Qt6::Qml Qt6::Quick Qt6::QuickTest omaseed-kitplugin omaseed-viewsplugin
    DEFINES QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
