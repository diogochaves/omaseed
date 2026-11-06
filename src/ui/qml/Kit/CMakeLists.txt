# Module Omaseed.Kit: themed components that take only Theme tokens.
# Keep QML_FILES one per line and alphabetical. Theme is Theme.qml, a singleton over the C++
# theme from omaseed-integration (registered into this module as ThemeBase by ThemeForeign.hpp).
qt_add_library(omaseed-kit STATIC)

# `pragma Singleton` alone builds fine, but qmllint only trusts the type if qmldir says so too;
# this must be set before qt_add_qml_module reads the source list.
set_source_files_properties(Glyphs.qml Fuzzy.qml Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE true)

# The skins' faces (#171, the console skin's design note "Seams"): whole-file bodies for the
# seams (Plate, SelectionMark, Meter, Lamp), compiled into this module under
# `skins/<skin>/faces/` so Theme.face() resolves them beside Theme.qml. They are loaded by URL,
# never by type name, so they stay out of qmldir (each skin's Plate.qml would clash).
set(kit_seams Plate SelectionMark Meter Lamp)
set(kit_faces "")
foreach(skin omarchy console)
    foreach(seam IN LISTS kit_seams)
        set(face "${PROJECT_SOURCE_DIR}/src/ui/skins/${skin}/faces/${seam}.qml")
        set_source_files_properties("${face}" PROPERTIES
            QT_RESOURCE_ALIAS "skins/${skin}/faces/${seam}.qml"
            QT_QML_SKIP_QMLDIR_ENTRY TRUE)
        list(APPEND kit_faces "${face}")
    endforeach()
endforeach()

qt_add_qml_module(omaseed-kit
    URI Omaseed.Kit
    VERSION 1.0
    SOURCES
        BackButtonEscape.cpp
        BackButtonEscape.hpp
        KeyWatch.cpp
        KeyWatch.hpp
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
        FaceLoader.qml
        Field.qml
        FocusRing.qml
        FormRow.qml
        Fuzzy.qml
        Glyph.qml
        Glyphs.qml
        Hairline.qml
        Hatch.qml
        Icon.qml
        IconButton.qml
        Ink.qml
        InputMouseArea.qml
        KeyBadge.qml
        KeyCap.qml
        KeyHints.qml
        KeyValue.qml
        Label.qml
        Lamp.qml
        Menu.qml
        Meter.qml
        Notice.qml
        Palette.qml
        PickList.qml
        Plate.qml
        ProgressLine.qml
        RailRow.qml
        RatioLadder.qml
        Scrim.qml
        ScrollBar.qml
        ScrollHint.qml
        SelectionMark.qml
        Sheet.qml
        Sparkline.qml
        SplitLayout.qml
        StackStrip.qml
        Stat.qml
        Strip.qml
        TableHeader.qml
        TableRow.qml
        Tabs.qml
        Theme.qml
        Toggle.qml
        Tooltip.qml
        WheelScroll.qml
        ${kit_faces}
)

target_link_libraries(omaseed-kit
    PRIVATE
        omaseed::integration
        omaseed::warnings
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickLayouts
)
