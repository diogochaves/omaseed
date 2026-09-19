# Module Omaseed.Kit: themed components that take only Theme tokens.
# Keep QML_FILES one per line and alphabetical. Theme itself is the C++ singleton from
# omaseed-integration, registered into this module by ThemeForeign.hpp.
qt_add_library(omaseed-kit STATIC)

qt_add_qml_module(omaseed-kit
    URI Omaseed.Kit
    VERSION 1.0
    SOURCES
        ThemeForeign.hpp
    QML_FILES
)

target_link_libraries(omaseed-kit
    PRIVATE
        omaseed::integration
        omaseed::warnings
        Qt6::Quick
)
