# The omaseed executable. The QML lives in three modules under qml/; each one is a static
# library whose plugin is linked here, so the types are registered from the resources.
qt_add_executable(omaseed
    main.cpp
)

add_subdirectory(qml)

target_link_libraries(omaseed
    PRIVATE
        omaseed::core
        omaseed::integration
        omaseed::warnings
        omaseed-kitplugin
        omaseed-qmlplugin
        omaseed-viewsplugin
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Widgets
)

set_target_properties(omaseed PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE
)
