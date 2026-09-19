omaseed_add_test(tst_appidentity
    SOURCES tst_appidentity.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_xdg
    SOURCES tst_xdg.cpp
    LIBRARIES omaseed::integration Qt6::DBus Qt6::Gui
)
