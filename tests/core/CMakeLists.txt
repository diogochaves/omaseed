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

omaseed_add_test(tst_peerinput
    SOURCES tst_peerinput.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_piecestates
    SOURCES tst_piecestates.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_torrentfile
    SOURCES tst_torrentfile.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# The live tests' URL guard (tests/LiveServer.hpp): nothing may reach :8080.
omaseed_add_test(tst_liveserver
    SOURCES tst_liveserver.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_torrentfile_live LIVE
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

omaseed_add_test(tst_serverdetector
    SOURCES tst_serverdetector.cpp
    LIBRARIES omaseed::core Qt6::Network
)

omaseed_add_test(tst_serverlistmodel
    SOURCES tst_serverlistmodel.cpp
    LIBRARIES omaseed::core Qt6Keychain::Qt6Keychain
)

omaseed_add_test(tst_secretstore
    SOURCES tst_secretstore.cpp
    LIBRARIES omaseed::core Qt6Keychain::Qt6Keychain
)

omaseed_add_test(tst_qbtclient_actions_fake
    SOURCES tst_qbtclient_actions_fake.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_qbtclient_actions_live LIVE
    SOURCES tst_qbtclient_actions_live.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_qbtclient_fake
    SOURCES tst_qbtclient_fake.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_qbtclient_live LIVE
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

omaseed_add_test(tst_categorymodel
    SOURCES tst_categorymodel.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_tagmodel
    SOURCES tst_tagmodel.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_trackermodel
    SOURCES tst_trackermodel.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_globalstate
    SOURCES tst_globalstate.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_filtermodel
    SOURCES tst_filtermodel.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_filtercounts
    SOURCES tst_filtercounts.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_statefile
    SOURCES tst_statefile.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_syncengine_fixtures
    SOURCES tst_syncengine_fixtures.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_syncengine_live LIVE
    SOURCES tst_syncengine_live.cpp
    LIBRARIES omaseed::core
)

# --- T3.4: the details pane ---------------------------------------------------------------
omaseed_add_test(tst_qbtclient_details_fake
    SOURCES tst_qbtclient_details_fake.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_torrentfilesmodel
    SOURCES tst_torrentfilesmodel.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_torrenttrackersmodel
    SOURCES tst_torrenttrackersmodel.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_torrentpeersmodel
    SOURCES tst_torrentpeersmodel.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_torrentdetails
    SOURCES tst_torrentdetails.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_torrentdetails_live LIVE
    SOURCES tst_torrentdetails_live.cpp
    LIBRARIES omaseed::core Qt6::Network
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# docs/11 D1 — the add sheet's file picker, over the .torrent fixtures.
omaseed_add_test(tst_addfilesmodel
    SOURCES tst_addfilesmodel.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)
