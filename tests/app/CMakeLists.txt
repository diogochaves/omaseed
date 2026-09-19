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

# T4.5 — the settings half of App, live against the strict server on :8082.
omaseed_add_test(tst_app_servers
    SOURCES tst_app_servers.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration Qt6::Network
)
