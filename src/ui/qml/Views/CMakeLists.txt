# Module Omaseed.Views: the screens, each fed by a model.
# Keep QML_FILES one per line and alphabetical.
qt_add_library(omaseed-views STATIC)

qt_add_qml_module(omaseed-views
    URI Omaseed.Views
    VERSION 1.0
    SOURCES
        DemoEnvForeign.hpp
    QML_FILES
        DemoDetails.qml
        DemoOverlays.qml
        DemoRail.qml
        DemoStates.qml
        DemoTorrents.qml
        Details.qml
        DetailsFilesTab.qml
        DetailsGeneralTab.qml
        DetailsPeersTab.qml
        DetailsTrackersTab.qml
        DimmedOverlay.qml
        DisconnectedBanner.qml
        EmptyState.qml
        FilteredEmpty.qml
        HelpSheet.qml
        LoadingState.qml
        MainView.qml
        Rail.qml
        RailGroup.qml
        ServerSheet.qml
        SettingsModal.qml
        SettingsRow.qml
        SettingsServerRow.qml
        ThemeGallery.qml
        TorrentTable.qml
)

target_link_libraries(omaseed-views
    PRIVATE
        omaseed-kitplugin
        omaseed::warnings
        Qt6::Quick
)
