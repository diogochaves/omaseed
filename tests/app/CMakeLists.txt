# The composition root. omaseed-qmlplugin is linked too: without it the QML engine in
# tst_app would not have module `Omaseed` (its types are registered by that static plugin).
omaseed_add_test(tst_app
    SOURCES tst_app.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration omaseed-qmlplugin Qt6::Qml
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
omaseed_add_test(tst_app_palettes_live
    SOURCES tst_app_palettes_live.cpp
    LIBRARIES omaseed::core
)

omaseed_add_test(tst_app_add
    SOURCES tst_app_add.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

# T4.3, against the :8081 playground; QSKIPs the whole file when it is not running.
omaseed_add_test(tst_app_actions
    SOURCES tst_app_actions.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
)
