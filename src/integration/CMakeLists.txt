# omaseed-integration: the desktop glue (D-Bus, tray, notifications, theme, xdg).
qt_add_library(omaseed-integration STATIC
    AppIdentity.cpp
    AppIdentity.hpp
    Notifier.cpp
    Notifier.hpp
)
add_library(omaseed::integration ALIAS omaseed-integration)

target_include_directories(omaseed-integration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(omaseed-integration
    PUBLIC omaseed::core Qt6::Core Qt6::DBus Qt6::Gui Qt6::Qml
    PRIVATE omaseed::warnings
)
