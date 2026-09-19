# Module Omaseed.Kit: themed components that take only Theme tokens.
# Keep QML_FILES one per line and alphabetical.
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(omaseed-kit STATIC)

qt_add_qml_module(omaseed-kit
    URI Omaseed.Kit
    VERSION 1.0
    QML_FILES
        Theme.qml
)

target_link_libraries(omaseed-kit
    PRIVATE
        omaseed::warnings
        Qt6::Quick
)
