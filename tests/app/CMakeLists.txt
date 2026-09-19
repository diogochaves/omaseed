# The composition root. omaseed-qmlplugin is linked too: without it the QML engine in
# tst_app would not have module `Omaseed` (its types are registered by that static plugin).
omaseed_add_test(tst_app
    SOURCES tst_app.cpp
    LIBRARIES omaseed::app omaseed::core omaseed::integration omaseed-qmlplugin Qt6::Qml
)
