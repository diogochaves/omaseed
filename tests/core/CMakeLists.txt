omaseed_add_test(tst_version
    SOURCES tst_version.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_apierror
    SOURCES tst_apierror.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_bencode
    SOURCES tst_bencode.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_magnetlink
    SOURCES tst_magnetlink.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_pathmap
    SOURCES tst_pathmap.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_torrentfile
    SOURCES tst_torrentfile.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_torrentfile_live
    SOURCES tst_torrentfile_live.cpp
    LIBRARIES omaseed::core Qt6::Network
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)
