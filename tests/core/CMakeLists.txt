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

omaseed_add_test(tst_server
    SOURCES tst_server.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_serverstore
    SOURCES tst_serverstore.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_secretstore
    SOURCES tst_secretstore.cpp
    LIBRARIES omaseed::core Qt6Keychain::Qt6Keychain
)

omaseed_add_test(tst_qbtclient_fake
    SOURCES tst_qbtclient_fake.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_qbtclient_live
    SOURCES tst_qbtclient_live.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_format
    SOURCES tst_format.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_torrentmodel
    SOURCES tst_torrentmodel.cpp
    LIBRARIES omaseed::core
)
