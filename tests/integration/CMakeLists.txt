omaseed_add_test(tst_appidentity
    SOURCES tst_appidentity.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_themereader
    SOURCES tst_themereader.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_themeresolver
    SOURCES tst_themeresolver.cpp
    LIBRARIES omaseed::integration
)
