# Desktop integration files: the .desktop entry, the D-Bus service file (DBusActivatable
# needs both to agree) and the app icon. The source files in this directory keep a plain
# `Exec=omaseed`, resolved on `$PATH`, which is what scripts/dev/install-desktop.sh uses for
# a dev build; `cmake --install` instead substitutes the real install prefix here, since a
# system install cannot rely on `$PATH`.
set(omaseed_app_id "io.github.diogochaves.omaseed")

function(omaseed_install_with_exec_path source_name dest_subdir)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${source_name}")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${source_name}")
    file(READ "${source}" contents)
    string(REPLACE "Exec=omaseed" "Exec=${CMAKE_INSTALL_FULL_BINDIR}/omaseed" contents "${contents}")
    file(GENERATE OUTPUT "${generated}" CONTENT "${contents}")
    install(FILES "${generated}" DESTINATION "${dest_subdir}")
endfunction()

omaseed_install_with_exec_path("${omaseed_app_id}.desktop" "${CMAKE_INSTALL_DATADIR}/applications")
omaseed_install_with_exec_path("${omaseed_app_id}.service" "${CMAKE_INSTALL_DATADIR}/dbus-1/services")

install(
    FILES "icons/hicolor/scalable/apps/${omaseed_app_id}.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
)
