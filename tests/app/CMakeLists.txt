# The composition root. omaseed-qmlplugin is linked too: without it the QML engine in
# tst_app would not have module `Omaseed` (its types are registered by that static plugin).
omaseed_add_test(tst_app LIVE
    SOURCES tst_app.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration omaseed-qmlplugin Qt6::Qml
)

# APP-7: every `--demo` stays offline; runs the real binary against a trap listener.
omaseed_add_test(tst_demo_offline
    SOURCES tst_demo_offline.cpp
    LIBRARIES omaseed::core Qt6::Network
    DEFINES OMASEED_BINARY="$<TARGET_FILE:omaseed>"
)
add_dependencies(tst_demo_offline omaseed)

# App glue that needs no server (install runner, path map, tray mode, demo isolation).
omaseed_add_test(tst_app_glue
    SOURCES tst_app_glue.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Widgets Qt6::Network
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #73: the shared menu of the tray and the bar widget, and "In the bar" (docs/33 §11).
omaseed_add_test(tst_app_barmenu
    SOURCES tst_app_barmenu.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Widgets
)

# #128: SIGTERM, SIGINT, SIGHUP and `omaseed --quit` quit through the tray's quit; the real
# binary, offscreen, on a private bus or none, against a fake qBittorrent.
omaseed_add_test(tst_quit
    SOURCES tst_quit.cpp
    LIBRARIES Qt6::Network Qt6::Sql
    DEFINES OMASEED_BINARY="$<TARGET_FILE:omaseed>"
)
add_dependencies(tst_quit omaseed)

# docs/DECISIONS.md C20 (#107): `p` stops an errored torrent and omaseed remembers the stop.
omaseed_add_test(tst_app_errored
    SOURCES tst_app_errored.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
)

# docs/DECISIONS.md M12: a torrent option shows at once, against a small fake server.
omaseed_add_test(tst_app_options
    SOURCES tst_app_options.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
)

omaseed_add_test(tst_app_notifications
    SOURCES tst_app_notifications.cpp ../integration/FakeNotificationServer.hpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::DBus
)

# T4.4 — state.json round trip and the palettes' demo-mode helpers.
omaseed_add_test(tst_app_tablestate
    SOURCES tst_app_tablestate.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
)

# T4.4 — category/tag creation against the :8081 playground (skips when it is not running).
omaseed_add_test(tst_app_palettes_live LIVE
    SOURCES tst_app_palettes_live.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_app_add LIVE
    SOURCES tst_app_add.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# Manual box M18: renames after the add, against a fake server (not live).
omaseed_add_test(tst_app_add_renames
    SOURCES tst_app_add_renames.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# T4.3, against the :8081 playground; QSKIPs the whole file when it is not running.
omaseed_add_test(tst_app_actions LIVE
    SOURCES tst_app_actions.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
)

# T4.5 — the settings half of App, live against the strict server on :8082.
omaseed_add_test(tst_app_servers LIVE
    SOURCES tst_app_servers.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
)

# N1 — the Files tab's priority and rename actions, live against the :8081 playground.
omaseed_add_test(tst_app_files_live LIVE
    SOURCES tst_app_files_live.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# docs/16's settings against a real qBittorrent the test starts and throws away (never the
# :8081/:8082 servers): every row's keys, the Web UI cut-offs, the API key rotation. Skips
# without qbittorrent-nox. Live for the lock: it starts servers and runs for a while.
omaseed_add_test(tst_qbtsettings_live LIVE
    SOURCES tst_qbtsettings_live.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network Qt6::Qml
    DEFINES OMASEED_QML_DIR="${PROJECT_SOURCE_DIR}/src/ui/qml"
            OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #13: the bar widget's D-Bus actions without a server (describe, demo add, window hand-offs).
omaseed_add_test(tst_statusservice
    SOURCES tst_statusservice.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #13: AddSources and the alt-speed calls against the :8081 playground (skips without it).
omaseed_add_test(tst_statusservice_live LIVE
    SOURCES tst_statusservice_live.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #44 (docs/29): a magnet's files before the add, against a small fake qBittorrent.
omaseed_add_test(tst_app_magnet
    SOURCES tst_app_magnet.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
)

# #32: export and import of a qBittorrent's settings, on the demo's offline server.
omaseed_add_test(tst_app_qbtbackup
    SOURCES tst_app_qbtbackup.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
)

# #33 (docs/24 §4–§5): torrents exported and imported through App, between two throwaway
# qBittorrents.
omaseed_add_test(tst_app_torrentsbackup_live LIVE
    SOURCES tst_app_torrentsbackup_live.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
)

# #97 (board 42): moving a folder, as the window sees it, with no server.
omaseed_add_test(tst_movecontroller
    SOURCES tst_movecontroller.cpp
    LIBRARIES omaseed::app omaseed::core
)
