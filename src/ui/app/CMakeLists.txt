# omaseed-app: the composition root. Pure C++ (no QML), so the executable and the tests can
# both build the whole app object graph and drive it.
qt_add_library(omaseed-app STATIC
    App.cpp
    App.hpp
    AppBarMenu.cpp
    AppServers.cpp
    AppSharedData.cpp
    AppTorrentsBackup.cpp
    DemoData.cpp
    DemoData.hpp
    StatusService.cpp
    StatusService.hpp
)
add_library(omaseed::app ALIAS omaseed-app)

target_include_directories(omaseed-app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")

target_link_libraries(omaseed-app
    PUBLIC omaseed::core omaseed::integration Qt6::Core Qt6::Qml
    # T4.3: App::copyToClipboard (QClipboard) and App::pickDirectory (QFileDialog).
    PRIVATE omaseed::warnings Qt6::Gui Qt6::Widgets
)
