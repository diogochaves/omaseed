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

# docs/UX-NOTES M12: a torrent option shows at once, against a small fake server.
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
)
