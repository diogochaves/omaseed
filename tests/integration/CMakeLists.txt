omaseed_add_test(tst_appidentity
    SOURCES tst_appidentity.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_notifier
    SOURCES tst_notifier.cpp
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
