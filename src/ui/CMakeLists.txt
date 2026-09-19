# The omaseed executable. The QML lives in three modules under qml/; each one is a static
# library whose plugin is linked here, so the types are registered from the resources.
qt_add_executable(omaseed
    main.cpp
)

add_subdirectory(app)
add_subdirectory(qml)

target_link_libraries(omaseed
    PRIVATE
        omaseed::app
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

# The binary itself (T1.4 left this for T5.1's packaging work; data/CMakeLists.txt installs
# the desktop integration files that go with it).
install(TARGETS omaseed
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
