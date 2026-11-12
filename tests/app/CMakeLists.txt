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

# #143: --help, --version and --theme-roles print with no display at all.
omaseed_add_test(tst_printonly
    SOURCES tst_printonly.cpp
    DEFINES OMASEED_BINARY="$<TARGET_FILE:omaseed>"
)
add_dependencies(tst_printonly omaseed)

# docs/DECISIONS.md C30 F7: every text role on every ground, in every theme of
# tests/fixtures/themes and every built-in skin (#171), as the built omaseed resolves them
# (`--theme-roles --skin`, offscreen, no window). A pair under 4.5:1 fails here, and so does
# a mark under WCAG 1.4.11's 3:1 on any ground it sits on (#200, C30 marks: --strict-marks).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(
        NAME contrast_board_check
        COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/dev/contrast-board.py"
                --check "$<TARGET_FILE:omaseed>"
                --themes-root "${PROJECT_SOURCE_DIR}/tests/fixtures/themes"
                --strict-marks
    )
    set_tests_properties(contrast_board_check PROPERTIES TIMEOUT 120)
    # #200: the marks pass on made-up roles, a mark that reads and ones built to fail, so the
    # strict check above can't pass by measuring nothing.
    add_test(
        NAME contrast_board_marks_selftest
        COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/dev/contrast-board.py"
                --self-test
    )
endif()

# docs/DECISIONS.md C20 (#107): `p` stops an errored torrent and omaseed remembers the stop.
omaseed_add_test(tst_app_errored
    SOURCES tst_app_errored.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
)

# #84 (C25 K4–K9): a server's test recognises its engine, against FakeEngines.
omaseed_add_test(tst_app_engines
    SOURCES tst_app_engines.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
)

# #85 (C14 E4, C25): a Transmission server driven in the app, against FakeTransmission and the
# daemon's recorded answers.
omaseed_add_test(tst_app_transmission
    SOURCES tst_app_transmission.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #88 (C40 X1, X3): Settings › Transmission › Backup's export in the app: the engine's exporter,
# whether its .torrent files are read from here, the folder given for them.
omaseed_add_test(tst_app_transmissionbackup
    SOURCES tst_app_transmissionbackup.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #88 (C40 X1, X4-X8): a torrents file imported into a Transmission in the app: the engine's
# importer, the sheet's data, the result's words, the links finished later.
omaseed_add_test(tst_app_transmissionimport
    SOURCES tst_app_transmissionimport.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #88 (C40 X1, X3-X8): a Deluge's torrents exported and imported in the app: the engine's
# exporter and importer, a daemon that tells no folder, the one label and the labels made
# first, the Label plugin off said before and after, the links finished later.
omaseed_add_test(tst_app_delugebackup
    SOURCES tst_app_delugebackup.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #87 (C14 E6, C25): a Deluge server driven in the app, against FakeDeluge and deluge-web's
# recorded answers.
omaseed_add_test(tst_app_deluge
    SOURCES tst_app_deluge.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #180 (DECISIONS C17 D1–D2): the skipped files' reading follows the server's file list, against a
# small fake server whose files sit in a temporary folder.
omaseed_add_test(tst_app_skipped
    SOURCES tst_app_skipped.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
)

# DECISIONS C18 D1–D3 (board 45 §2): a table action's outcome is the strip, against a small
# fake server.
omaseed_add_test(tst_app_outcomes
    SOURCES tst_app_outcomes.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
)

# DECISIONS C38 (#236): omaseed's own renames are the story's record, per server, against a small
# fake server.
omaseed_add_test(tst_app_renames
    SOURCES tst_app_renames.cpp
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

# DECISIONS C28 (#159): a tick that doesn't fit on a FAT loop image waits, through App, live
# against the :8081 playground (udisks mounts the image as the user).
omaseed_add_test(tst_app_tickroom_live LIVE
    SOURCES tst_app_tickroom_live.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
)

# docs/16's settings against a real qBittorrent the test starts and throws away (never the
# :8081/:8082 servers): every row's keys, the Web UI cut-offs, the API key rotation. Skips
# without qbittorrent-nox. Live for the lock: it starts servers and runs for a while.
# The schema imports Omaseed.Kit (the speed presets' Glyphs): the Kit plugin is linked in.
omaseed_add_test(tst_qbtsettings_live LIVE
    SOURCES tst_qbtsettings_live.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network Qt6::Qml
              omaseed-kitplugin
    DEFINES OMASEED_QML_DIR="${PROJECT_SOURCE_DIR}/src/ui/qml"
            OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #13: the bar widget's D-Bus actions without a server (describe, demo add, window hand-offs).
omaseed_add_test(tst_statusservice
    SOURCES tst_statusservice.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# #218 (DECISIONS C35 W1, W2, W6): where an add from outside the window opens, and the raise.
omaseed_add_test(tst_addroute
    SOURCES tst_addroute.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
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

# #146: creating a torrent from a folder, against a fake qBittorrent.
omaseed_add_test(tst_app_create
    SOURCES tst_app_create.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network Qt6::Gui
)

# #145: search, against a fake qBittorrent with invented plugins.
omaseed_add_test(tst_app_search
    SOURCES tst_app_search.cpp
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
