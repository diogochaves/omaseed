omaseed_add_test(tst_appidentity
    SOURCES tst_appidentity.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_notifier
    SOURCES tst_notifier.cpp FakeNotificationServer.hpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_xdg
    SOURCES tst_xdg.cpp
    LIBRARIES omaseed::integration Qt6::DBus Qt6::Gui
)

omaseed_add_test(tst_themereader
    SOURCES tst_themereader.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_themeresolver
    SOURCES tst_themeresolver.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_singleinstance
    SOURCES tst_singleinstance.cpp
    LIBRARIES omaseed::integration
    DEFINES OMASEED_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)

omaseed_add_test(tst_tray
    SOURCES tst_tray.cpp
    LIBRARIES omaseed::integration Qt6::Svg Qt6::Widgets
)

omaseed_add_test(tst_themecontrast
    SOURCES tst_themecontrast.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_statuswriter
    SOURCES tst_statuswriter.cpp
    LIBRARIES omaseed::integration
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
