# omaseed-integration: the desktop glue (D-Bus, tray, notifications, theme, xdg).
qt_add_library(omaseed-integration STATIC
    AgentBus.cpp
    AgentBus.hpp
    AgentClient.cpp
    AgentClient.hpp
    AgentGuide.cpp
    AgentGuide.hpp
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
    DesktopSkin.cpp
    DesktopSkin.hpp
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
    SkinReader.cpp
    SkinReader.hpp
    SkinWords.cpp
    SkinWords.hpp
    SkillLinker.cpp
    SkillLinker.hpp
    SkillWords.cpp
    SkillWords.hpp
    StatusWriter.cpp
    StatusWriter.hpp
    Theme.cpp
    Theme.hpp
    ThemeInk.cpp
    ThemeInk.hpp
    ThemeReader.cpp
    ThemeReader.hpp
    StatusBus.cpp
    StatusBus.hpp
    Tray.cpp
    Tray.hpp
    UserSkins.cpp
    UserSkins.hpp
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

# The guide for coding agents (DECISIONS C48): omaseed's skill, data/agents/skill/, compiled
# in as it is installed, for --skill, omaseed skill and AGENTS.md. One source, no copy here.
set(OMASEED_SKILL_DATA "${PROJECT_SOURCE_DIR}/data/agents")
qt_add_resources(omaseed-integration "omaseed-agent-skill"
    PREFIX "/omaseed/agents"
    BASE "${OMASEED_SKILL_DATA}"
    FILES
        "${OMASEED_SKILL_DATA}/skill/SKILL.md"
        "${OMASEED_SKILL_DATA}/skill/ref/library.md"
        "${OMASEED_SKILL_DATA}/skill/ref/skins.md"
        "${OMASEED_SKILL_DATA}/skill/ref/files.md"
)

# The built-in skins (#171): Omarchy's, with no overrides, and Console's. They live with the
# QML they dress (src/ui/skins), but SkinReader reads them, so they are this library's.
qt_add_resources(omaseed-integration "omaseed-skins"
    PREFIX "/omaseed/skins"
    BASE "${PROJECT_SOURCE_DIR}/src/ui/skins"
    FILES
        "${PROJECT_SOURCE_DIR}/src/ui/skins/omarchy/skin.json"
        "${PROJECT_SOURCE_DIR}/src/ui/skins/console/skin.json"
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
