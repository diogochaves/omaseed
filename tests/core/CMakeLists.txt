omaseed_add_test(tst_version
    SOURCES tst_version.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_apierror
    SOURCES tst_apierror.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_errorwords
    SOURCES tst_errorwords.cpp
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

omaseed_add_test(tst_searchwords
    SOURCES tst_searchwords.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_metadatalookup
    SOURCES tst_metadatalookup.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_torrentextras
    SOURCES tst_torrentextras.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_privatetrackers
    SOURCES tst_privatetrackers.cpp
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

omaseed_add_test(tst_enginekind
    SOURCES tst_enginekind.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_enginerecognizer
    SOURCES tst_enginerecognizer.cpp
    LIBRARIES omaseed::core Qt6::Network
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
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
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
omaseed_add_test(tst_qbtengine_details_fake
    SOURCES tst_qbtengine_details_fake.cpp
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

omaseed_add_test(tst_weeklyrecap
    SOURCES tst_weeklyrecap.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_beeswarm
    SOURCES tst_beeswarm.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_speedsets
    SOURCES tst_speedsets.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_altschedule
    SOURCES tst_altschedule.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_altspeed
    SOURCES tst_altspeed.cpp
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

omaseed_add_test(tst_torrentsbackup
    SOURCES tst_torrentsbackup.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

omaseed_add_test(tst_torrentsbackup_live LIVE
    SOURCES tst_torrentsbackup_live.cpp
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

# docs/44 D13 (#121): what a failed move left in the destination.
omaseed_add_test(tst_partialcopies
    SOURCES tst_partialcopies.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_problemlog
    SOURCES tst_problemlog.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_failurelines
    SOURCES tst_failurelines.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_problemlog_failures
    SOURCES tst_problemlog_failures.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_serverlog
    SOURCES tst_serverlog.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_serverlog_live LIVE
    SOURCES tst_serverlog_live.cpp
    LIBRARIES omaseed::core
)

# #32, docs/24 §5–§6: a qBittorrent's settings as a file, and the import's preview.
omaseed_add_test(tst_qbtsettingsbackup
    SOURCES tst_qbtsettingsbackup.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #70, docs/34 D8 a and D6 a: a file known by its contents; another server's settings from
# omaseed's copies when it can't be read.
omaseed_add_test(tst_backupformat
    SOURCES tst_backupformat.cpp
    LIBRARIES omaseed::core Qt6::Network
)

# #70, docs/34 D6 a: another server's settings through a client of its own, from the :8081
# playground and a throwaway qBittorrent with a password login.
omaseed_add_test(tst_qbtsettingsfetch_live LIVE
    SOURCES tst_qbtsettingsfetch_live.cpp
    LIBRARIES omaseed::core Qt6::Network
)

# #32: export from the :8081 playground; a round trip between two throwaway qBittorrents.
omaseed_add_test(tst_qbtsettingsbackup_live LIVE
    SOURCES tst_qbtsettingsbackup_live.cpp
    LIBRARIES omaseed::core Qt6::Network
)

# #81: recorded qBittorrent replies give the same rows, roles and counts as before E1.
omaseed_add_test(tst_qbt_golden
    SOURCES tst_qbt_golden.cpp
    LIBRARIES omaseed::core
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #81: the engine-neutral layer, and the one translator from qBittorrent into it.
omaseed_add_test(tst_torrentpatch
    SOURCES tst_torrentpatch.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_qbttranslate
    SOURCES tst_qbttranslate.cpp
    LIBRARIES omaseed::core
)

# #82: the interface itself, with a fake engine of the test's own driving SyncEngine.
omaseed_add_test(tst_engineclient
    SOURCES tst_engineclient.cpp
    LIBRARIES omaseed::core
)

# #82: qBittorrent behind EngineClient, both ways, against a fake server.
omaseed_add_test(tst_qbtengine
    SOURCES tst_qbtengine.cpp
    LIBRARIES omaseed::core Qt6::Network
)

omaseed_add_test(tst_foldertree
    SOURCES tst_foldertree.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_foldermodel
    SOURCES tst_foldermodel.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_loosefiles
    SOURCES tst_loosefiles.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_loosecarry
    SOURCES tst_loosecarry.cpp
    LIBRARIES omaseed::core
)
# The same test's full-disk case, on a 1 MiB tmpfs in a user namespace (no root); skipped
# where user namespaces aren't allowed.
add_test(NAME tst_loosecarry_full
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/run-small-fs.sh" $<TARGET_FILE:tst_loosecarry> fullDestination)
set_tests_properties(tst_loosecarry_full PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tests/data-home"
)

omaseed_add_test(tst_moveplan
    SOURCES tst_moveplan.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_actionoutcome
    SOURCES tst_actionoutcome.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_movehistory
    SOURCES tst_movehistory.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_movejob
    SOURCES tst_movejob.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_movejob_live LIVE
    SOURCES tst_movejob_live.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_skippedondisk
    SOURCES tst_skippedondisk.cpp
    LIBRARIES omaseed::core
)

# Board 44 on a real ntfs3 loop image through udisks, against the playground (#118, #120).
omaseed_add_test(tst_skipped_ntfs3_live LIVE
    SOURCES tst_skipped_ntfs3_live.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_selectionhold
    SOURCES tst_selectionhold.cpp
    LIBRARIES omaseed::core
)
