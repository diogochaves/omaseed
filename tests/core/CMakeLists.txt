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

omaseed_add_test(tst_librarymatch
    SOURCES tst_librarymatch.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_magnetlink
    SOURCES tst_magnetlink.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_freespace
    SOURCES tst_freespace.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_pathmap
    SOURCES tst_pathmap.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_shareddata
    SOURCES tst_shareddata.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_renameplan
    SOURCES tst_renameplan.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_renamedisk
    SOURCES tst_renamedisk.cpp
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

omaseed_add_test(tst_torrentfolderscan
    SOURCES tst_torrentfolderscan.cpp
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

omaseed_add_test(tst_certificatepin
    SOURCES tst_certificatepin.cpp
    LIBRARIES omaseed::core Qt6::Network
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# Trust on first use (issue #2) against a qbittorrent-nox of its own serving HTTPS.
omaseed_add_test(tst_qbtclient_tls_live LIVE
    SOURCES tst_qbtclient_tls_live.cpp
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

omaseed_add_test(tst_serverpreferences
    SOURCES tst_serverpreferences.cpp
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

omaseed_add_test(tst_trackermerge
    SOURCES tst_trackermerge.cpp
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

omaseed_add_test(tst_filesviewstate
    SOURCES tst_filesviewstate.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_syncengine_fixtures
    SOURCES tst_syncengine_fixtures.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_syncengine_pending
    SOURCES tst_syncengine_pending.cpp
    LIBRARIES omaseed::core
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

omaseed_add_test(tst_torrentwebseedsmodel
    SOURCES tst_torrentwebseedsmodel.cpp
    LIBRARIES omaseed::core
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

omaseed_add_test(tst_webseeds_live LIVE
    SOURCES tst_webseeds_live.cpp
    LIBRARIES omaseed::core Qt6::Network
)

# docs/11 D1 — the add sheet's file picker, over the .torrent fixtures.
omaseed_add_test(tst_addfilesmodel
    SOURCES tst_addfilesmodel.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_seedledger
    SOURCES tst_seedledger.cpp
    LIBRARIES omaseed::core Qt6::Sql
)
# A zone whose clocks change, for the daylight-saving test; the rest is zone-agnostic.
set_tests_properties(tst_seedledger PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=qt.qml.*=false;TZ=Europe/Lisbon"
)

omaseed_add_test(tst_seedconsole
    SOURCES tst_seedconsole.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_beeswarm
    SOURCES tst_beeswarm.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_statusdigest
    SOURCES tst_statusdigest.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_tararchive
    SOURCES tst_tararchive.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_omaseedbackup
    SOURCES tst_omaseedbackup.cpp
    LIBRARIES omaseed::core Qt6::Sql
)

omaseed_add_test(tst_torrentproblem
    SOURCES tst_torrentproblem.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_problemlog
    SOURCES tst_problemlog.cpp
    LIBRARIES omaseed::core
)
