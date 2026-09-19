# Module Omaseed.Views: the screens, each fed by a model.
# Keep QML_FILES one per line and alphabetical.
qt_add_library(omaseed-views STATIC)

qt_add_qml_module(omaseed-views
    URI Omaseed.Views
    VERSION 1.0
    QML_FILES
        ThemeGallery.qml
)

target_link_libraries(omaseed-views
    PRIVATE
        omaseed-kitplugin
        omaseed::warnings
        Qt6::Quick
)
