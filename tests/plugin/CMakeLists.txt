# The companion bar widget's Format.js and Status.js (#16). Its QML needs the Omarchy shell
# and is checked in an omabox instead.
omaseed_add_test(tst_plugin
    SOURCES tst_plugin.cpp
    LIBRARIES Qt6::Qml Qt6::Quick Qt6::QuickTest
    DEFINES QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
