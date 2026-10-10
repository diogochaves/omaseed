# Module Omaseed.Kit: themed components that take only Theme tokens.
# Keep QML_FILES one per line and alphabetical. Theme itself is the C++ singleton from
# omaseed-integration, registered into this module by ThemeForeign.hpp.
qt_add_library(omaseed-kit STATIC)

# `pragma Singleton` alone builds fine, but qmllint only trusts the type if qmldir says so too;
# this must be set before qt_add_qml_module reads the source list.
set_source_files_properties(Glyphs.qml Fuzzy.qml PROPERTIES QT_QML_SINGLETON_TYPE true)

qt_add_qml_module(omaseed-kit
    URI Omaseed.Kit
    VERSION 1.0
    SOURCES
        RatioDots.cpp
        RatioDots.hpp
        ThemeForeign.hpp
    QML_FILES
        AreaChart.qml
        Banner.qml
        Button.qml
        Chip.qml
        ConfirmDialog.qml
        DayGrid.qml
        Field.qml
        FilterMenu.qml
        FocusRing.qml
        FormRow.qml
        Fuzzy.qml
        Glyph.qml
        Glyphs.qml
        Hairline.qml
        Hatch.qml
        Hero.qml
        Icon.qml
        IconButton.qml
        KeyCap.qml
        KeyHints.qml
        KeyValue.qml
        Label.qml
        Menu.qml
        Palette.qml
        ProgressLine.qml
        RailRow.qml
        RatioLadder.qml
        Scrim.qml
        ScrollBar.qml
        ScrollHint.qml
        Sheet.qml
        Sparkline.qml
        SplitLayout.qml
        StackStrip.qml
        Stat.qml
        Strip.qml
        TableHeader.qml
        TableRow.qml
        Tabs.qml
        Toggle.qml
        Tooltip.qml
        WheelScroll.qml
)

target_link_libraries(omaseed-kit
    PRIVATE
        omaseed::integration
        omaseed::warnings
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickLayouts
)
