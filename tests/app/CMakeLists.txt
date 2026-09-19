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

omaseed_add_test(tst_app_add
    SOURCES tst_app_add.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)
