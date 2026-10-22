# omaseed-integration: the desktop glue (D-Bus, tray, notifications, theme, xdg).
qt_add_library(omaseed-integration STATIC
    AppIdentity.cpp
    AppIdentity.hpp
    Autostart.cpp
    Autostart.hpp
    BarMenu.cpp
    BarMenu.hpp
    BarWidget.cpp
    BarWidget.hpp
    BarWidgetInstaller.cpp
    BarWidgetInstaller.hpp
    HyprlandEvents.cpp
    HyprlandEvents.hpp
    MotionPreference.cpp
    MotionPreference.hpp
    Notifier.cpp
    Notifier.hpp
    QuitSignals.cpp
    QuitSignals.hpp
    SingleInstance.cpp
    SingleInstance.hpp
    StatusWriter.cpp
    StatusWriter.hpp
    Theme.cpp
    Theme.hpp
    ThemeReader.cpp
    ThemeReader.hpp
    StatusBus.cpp
    StatusBus.hpp
    Tray.cpp
    Tray.hpp
    WidgetHeartbeat.cpp
    WidgetHeartbeat.hpp
    Xdg.cpp
    Xdg.hpp
)
add_library(omaseed::integration ALIAS omaseed-integration)

# The fallback palette, so omaseed has a theme without Omarchy installed.
qt_add_resources(omaseed-integration "omaseed-theme-fallback"
    PREFIX "/omaseed"
    BASE resources
    FILES resources/theme/tokyo-night/colors.toml
)

# The five tray icon states, SVG templates tinted from the theme at render time.
qt_add_resources(omaseed-integration "omaseed-tray-icons"
    PREFIX "/omaseed"
    BASE resources
    FILES
        resources/tray/altspeed.svg
        resources/tray/disconnected.svg
        resources/tray/downloading.svg
        resources/tray/error.svg
        resources/tray/idle.svg
)

target_include_directories(omaseed-integration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(omaseed-integration
    PUBLIC omaseed::core Qt6::Core Qt6::DBus Qt6::Gui Qt6::Network Qt6::Qml Qt6::Svg Qt6::Widgets
    PRIVATE omaseed::warnings
)
