# omaseed-integration: the desktop glue (D-Bus, tray, notifications, theme, xdg).
qt_add_library(omaseed-integration STATIC
    AppIdentity.cpp
    AppIdentity.hpp
    Notifier.cpp
    Notifier.hpp
    Theme.cpp
    Theme.hpp
    ThemeReader.cpp
    ThemeReader.hpp
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

target_include_directories(omaseed-integration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(omaseed-integration
    PUBLIC omaseed::core Qt6::Core Qt6::DBus Qt6::Gui Qt6::Qml
    PRIVATE omaseed::warnings
)
