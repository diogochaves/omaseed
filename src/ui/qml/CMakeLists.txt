# Module Omaseed: the application shell. Keep QML_FILES one per line and alphabetical.
add_subdirectory(Kit)
add_subdirectory(Views)

qt_add_library(omaseed-qml STATIC)

# `Keymap` is the single table of keys; it must be one object, not one per import. The
# property has to be set before the module is declared or the qmldir loses the `singleton`.
set_source_files_properties(Keymap.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(omaseed-qml
    URI Omaseed
    VERSION 1.0
    SOURCES
        AppForeign.hpp
        CoreForeign.hpp
        XdgForeign.hpp
    QML_FILES
        Keymap.qml
        Main.qml
)

target_link_libraries(omaseed-qml
    PRIVATE
        omaseed-kitplugin
        omaseed-viewsplugin
        omaseed::app
        omaseed::integration
        omaseed::warnings
        Qt6::Quick
)
