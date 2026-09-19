# Module Omaseed: the application shell. Keep QML_FILES one per line and alphabetical.
add_subdirectory(Kit)
add_subdirectory(Views)

qt_add_library(omaseed-qml STATIC)

qt_add_qml_module(omaseed-qml
    URI Omaseed
    VERSION 1.0
    SOURCES
        AppForeign.hpp
        CoreForeign.hpp
        XdgForeign.hpp
    QML_FILES
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
