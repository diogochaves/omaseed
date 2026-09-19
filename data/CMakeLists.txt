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

# hicolor also wants fixed-size PNGs (some launchers/panels do not rasterise SVG icons, and
# icon caches key off the size directories). Rendered from the SVG at build time rather than
# committed, so the repo carries one source-of-truth icon; requires rsvg-convert (preferred,
# librsvg) or ImageMagick's `magick`/`convert` (T5.1). If neither tool is present the PNGs are
# simply not built/installed -- the SVG alone is still a valid hicolor icon -- and a warning
# is printed once at configure time so a packaging build notices.
find_program(OMASEED_RSVG_CONVERT rsvg-convert)
find_program(OMASEED_MAGICK NAMES magick convert)

set(omaseed_icon_sizes 16 24 32 48 64 128 256)
set(omaseed_icon_svg "${CMAKE_CURRENT_SOURCE_DIR}/icons/hicolor/scalable/apps/${omaseed_app_id}.svg")
set(omaseed_icon_pngs "")

if(OMASEED_RSVG_CONVERT OR OMASEED_MAGICK)
    foreach(size IN LISTS omaseed_icon_sizes)
        set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/icons/hicolor/${size}x${size}/apps")
        set(out_png "${out_dir}/${omaseed_app_id}.png")
        if(OMASEED_RSVG_CONVERT)
            add_custom_command(
                OUTPUT "${out_png}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
                COMMAND "${OMASEED_RSVG_CONVERT}" -w "${size}" -h "${size}" "${omaseed_icon_svg}" -o "${out_png}"
                DEPENDS "${omaseed_icon_svg}"
                COMMENT "Rendering ${size}x${size} app icon with rsvg-convert"
                VERBATIM
            )
        else()
            add_custom_command(
                OUTPUT "${out_png}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
                COMMAND "${OMASEED_MAGICK}" -background none -resize "${size}x${size}" "${omaseed_icon_svg}" "${out_png}"
                DEPENDS "${omaseed_icon_svg}"
                COMMENT "Rendering ${size}x${size} app icon with ImageMagick"
                VERBATIM
            )
        endif()
        list(APPEND omaseed_icon_pngs "${out_png}")
    endforeach()

    add_custom_target(omaseed-icons ALL DEPENDS ${omaseed_icon_pngs})

    foreach(size IN LISTS omaseed_icon_sizes)
        install(
            FILES "${CMAKE_CURRENT_BINARY_DIR}/icons/hicolor/${size}x${size}/apps/${omaseed_app_id}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
        )
    endforeach()
else()
    message(WARNING
        "Neither rsvg-convert nor ImageMagick (magick/convert) found: hicolor PNG app icons "
        "will not be built. The scalable SVG is still installed. Install librsvg (preferred) "
        "or imagemagick to get the fixed-size icons.")
endif()

# MIT licence text (T5.1): every package under this prefix ships its own copy, per Arch
# convention (share/licenses/<pkgname>/LICENSE).
install(
    FILES "${CMAKE_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/omaseed"
)

# Install rules for the Omarchy integration extras (T1.8). One line per file: T5.1's
# PKGBUILD places/documents the user-level copies (hook dir, hypr module, menu fragment);
# this only stages the packaged copies under the data prefix.

install(FILES hyprland/omaseed.lua
    DESTINATION "${CMAKE_INSTALL_DATADIR}/omaseed/hyprland")
install(PROGRAMS hooks/theme-set.d/omaseed
    DESTINATION "${CMAKE_INSTALL_DATADIR}/omaseed/hooks/theme-set.d")
install(FILES omarchy-menu.jsonc
    DESTINATION "${CMAKE_INSTALL_DATADIR}/omaseed")
