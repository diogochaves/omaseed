-- omaseed Hyprland integration (T1.8).
--
-- Deviation from the plan: the plan describes this as a classic hyprland.conf
-- `windowrule = ...` snippet. This Omarchy install (Hyprland 0.56.2) configures Hyprland
-- natively in Lua -- there is no ~/.config/hypr/hyprland.conf and `hyprctl keyword` refuses
-- to run ("keyword can't work with non-legacy parsers. Use eval."). Window rules are written
-- with Omarchy's `o.window(match, rules)` helper (see
-- /usr/share/omarchy/default/hypr/helpers.lua and .../windows.lua), which calls Hyprland's
-- native `hl.window_rule(rules)`. This file follows that convention instead of the plan's
-- literal syntax; see the README "Omarchy integration" section and the T1.8 report for why.
--
-- Installed to ~/.config/hypr/omaseed.lua by scripts/dev/install-omarchy-extras.sh. It is
-- NOT auto-loaded: Omarchy only auto-requires a fixed list of user files from
-- ~/.config/hypr/hyprland.lua (monitors, input, bindings, looknfeel, autostart) plus
-- anything under ~/.config/hypr/mods/. Add one line near the bottom of hyprland.lua,
-- alongside the other `require("hypr...")` lines:
--
--   require("hypr.omaseed")
--
-- The install script prints this line; it does not edit hyprland.lua for you.

-- The app-id / window class omaseed reports (see AppIdentity::applicationId(), set with
-- QGuiApplication::setDesktopFileName() in src/ui/main.cpp).
local class = "io.github.diogochaves.omaseed"

-- Opt out of Omarchy 4.0.x's default window opacity.
--
-- Omarchy 4.0.x tags every window `+default-opacity` and then applies `opacity 0.985 0.96`
-- to whatever still carries that tag once every app-specific file has had a chance to remove
-- it (see /usr/share/omarchy/default/hypr/windows.lua, loaded before this file). omaseed
-- paints its own ground panel (Theme.windowAlpha in QML): opaque by default, like every
-- Omarchy 4.5 app (docs/DECISIONS.md C24 H13), or, when Settings > Appearance > Window says
-- translucent, the shell launcher's alpha (Q1). Compositor-level opacity multiplies on top
-- of window content, so leaving the tag in place would double a translucent ground and give
-- any fully-opaque chrome (menus, the tray icon, notifications) an unwanted 1.5-4%
-- translucency. omaseed is the only source of its own translucency: drop the tag and force
-- the window itself to full opacity.
--
-- Omarchy 4.5 has no `default-opacity` tag: apps are opaque unless they opt in
-- (`o.transparent_window`), so there the tag removal does nothing and `opacity "1 1"` only
-- restates the default. It does not get in the way of 4.5's Super+Backspace, which opts a
-- window in with the `transparent` tag: in a 4.5.0rc1 box with this file loaded, the toggle
-- still takes omaseed to 0.985 / 0.96 and back (#111). The rule stays for 4.0.x.
o.window(class, { tag = "-default-opacity", opacity = "1 1" })

-- First run needs no rule: its window fixes its own size (min = max, 875 x 600), which
-- Hyprland floats and centres by itself, and it maps again unlocked, so tiled, when setup is
-- done (src/ui/qml/Main.qml). The title-matched rule that used to sit here never matched:
-- the window never took the "omaseed · first run" title it expected.

-- The add window (docs/DECISIONS.md C35 W3, docs/54 §3): an add from outside the main window
-- opens the add sheet in a window of its own, floating and centred where you are. It fixes its
-- size to the sheet's, so Hyprland floats it without this rule; the rule covers the moment
-- between a size change and the next frame. It matches the title, not the class: Qt on
-- Wayland gives every window of an app the one app id, so the class can't tell the add window
-- from the main one. The title is fixed, `omaseed · add`, never translated
-- (src/ui/qml/Views/AddWindow.qml).
--
-- Deviation from the board's line, which also matched `class`: `o.window` records the class
-- of a float rule as an app that floats on its own, and Omarchy's floating workspace modes
-- leave such apps out of their open rule (/usr/share/omarchy/default/hypr/workspace-modes.lua),
-- so naming the class here would take the main window out of those modes too. Matching the
-- title as well records only the add window.
o.window({ initial_title = "^omaseed · add$", title = "^omaseed · add$" }, { float = true, center = true })

-- Touchpad scrolling at the terminals' pace. Omarchy slows every touchpad to
-- `scroll_factor = 0.4` and gives terminals 1.5 back per window
-- (/usr/share/omarchy/default/hypr/input.lua); omaseed's long lists want the same.
o.window(class, { scroll_touchpad = 1.5 })
