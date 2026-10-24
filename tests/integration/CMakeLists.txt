omaseed_add_test(tst_appidentity
    SOURCES tst_appidentity.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_autostart
    SOURCES tst_autostart.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_notifier
    SOURCES tst_notifier.cpp FakeNotificationServer.hpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_motionpreference
    SOURCES tst_motionpreference.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_xdg
    SOURCES tst_xdg.cpp
    LIBRARIES omaseed::integration Qt6::DBus Qt6::Gui
)

omaseed_add_test(tst_themereader
    SOURCES tst_themereader.cpp
    LIBRARIES omaseed::integration
)

# #171: a skin file read and checked, all or nothing (the console skin's design note).
omaseed_add_test(tst_skinreader
    SOURCES tst_skinreader.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_themeresolver
    SOURCES tst_themeresolver.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_singleinstance
    SOURCES tst_singleinstance.cpp
    LIBRARIES omaseed::integration
    DEFINES OMASEED_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)

omaseed_add_test(tst_tray
    SOURCES tst_tray.cpp
    LIBRARIES omaseed::integration Qt6::Svg Qt6::Widgets
)

# tests/fixtures/themes: Omarchy's own themes' colors.toml (MIT), every one checked on every
# ground (docs/DECISIONS.md C30 F7).
omaseed_add_test(tst_themecontrast
    SOURCES tst_themecontrast.cpp
    LIBRARIES omaseed::integration
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# #76: `omaseed --theme-roles`, the roles as Theme resolved them, as JSON.
omaseed_add_test(tst_themeroles
    SOURCES tst_themeroles.cpp
    LIBRARIES omaseed::integration
)

# Under Hyprland, Hyprland owns the window edge and its corners.
omaseed_add_test(tst_themecorners
    SOURCES tst_themecorners.cpp
    LIBRARIES omaseed::integration
)

# #14: the bar widget's heartbeat and the tray icon rule it drives.
omaseed_add_test(tst_widgetheartbeat
    SOURCES tst_widgetheartbeat.cpp
    LIBRARIES omaseed::integration
)

# #13: io.github.diogochaves.omaseed.Status on a private dbus-daemon.
# #73: the shared menu of the tray and the bar widget (docs/33 §11).
omaseed_add_test(tst_barmenu
    SOURCES tst_barmenu.cpp
    LIBRARIES omaseed::integration
)

omaseed_add_test(tst_statusbus
    SOURCES tst_statusbus.cpp
    LIBRARIES omaseed::integration Qt6::DBus
)

omaseed_add_test(tst_statuswriter
    SOURCES tst_statuswriter.cpp
    LIBRARIES omaseed::integration
    DEFINES OMASEED_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

omaseed_add_test(tst_barwidgetinstaller
    SOURCES tst_barwidgetinstaller.cpp
    LIBRARIES omaseed::integration
)

# #70, docs/34 D7 a, D9 a: omaseed's two archives as MIME types, installed (the "mime"
# component, into a temporary prefix) and read back; the .desktop entry does not open them.
omaseed_add_test(tst_mimetypes
    SOURCES tst_mimetypes.cpp
    DEFINES
        OMASEED_CMAKE_COMMAND="${CMAKE_COMMAND}"
        OMASEED_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
)
