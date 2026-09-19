omaseed_add_test(tst_version
    SOURCES tst_version.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_apierror
    SOURCES tst_apierror.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_server
    SOURCES tst_server.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_serverstore
    SOURCES tst_serverstore.cpp
    LIBRARIES omaseed::core
)
