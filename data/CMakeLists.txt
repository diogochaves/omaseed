# Install rules for the Omarchy integration extras (T1.8). One line per file: T5.1's
# PKGBUILD places/documents the user-level copies (hook dir, hypr module, menu fragment);
# this only stages the packaged copies under the data prefix.
include(GNUInstallDirs)

install(FILES hyprland/omaseed.lua
    DESTINATION "${CMAKE_INSTALL_DATADIR}/omaseed/hyprland")
install(PROGRAMS hooks/theme-set.d/omaseed
    DESTINATION "${CMAKE_INSTALL_DATADIR}/omaseed/hooks/theme-set.d")
install(FILES omarchy-menu.jsonc
    DESTINATION "${CMAKE_INSTALL_DATADIR}/omaseed")
