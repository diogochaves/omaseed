omaseed_add_test(tst_appidentity
    SOURCES tst_appidentity.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_singleinstance
    SOURCES tst_singleinstance.cpp
    LIBRARIES omaseed::integration
    DEFINES OMASEED_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)
