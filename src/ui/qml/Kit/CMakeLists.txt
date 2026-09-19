# Module Omaseed.Kit: themed components that take only Theme tokens.
# Keep QML_FILES one per line and alphabetical. Theme itself is the C++ singleton from
# omaseed-integration, registered into this module by ThemeForeign.hpp.
qt_add_library(omaseed-kit STATIC)

# `pragma Singleton` alone builds fine, but qmllint only trusts the type if qmldir says so too;
# this must be set before qt_add_qml_module reads the source list.
set_source_files_properties(Glyphs.qml PROPERTIES QT_QML_SINGLETON_TYPE true)

qt_add_qml_module(omaseed-kit
    URI Omaseed.Kit
    VERSION 1.0
    SOURCES
        ThemeForeign.hpp
    QML_FILES
        Banner.qml
        Button.qml
        Chip.qml
        Field.qml
        Glyphs.qml
        Hairline.qml
        Label.qml
        Stat.qml
        Toggle.qml
)

target_link_libraries(omaseed-kit
    PRIVATE
        omaseed::integration
        omaseed::warnings
        Qt6::Quick
)
