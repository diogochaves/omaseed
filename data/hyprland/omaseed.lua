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

-- Opt out of Omarchy's default window opacity.
--
-- Omarchy tags every window `+default-opacity` and then applies `opacity 0.985 0.96` to
-- whatever still carries that tag once every app-specific file has had a chance to remove
-- it (see /usr/share/omarchy/default/hypr/windows.lua, loaded before this file). omaseed
-- paints its own translucent "ground" panel per the approved spec (docs/05-ui-spec-v2.html:
-- the base surface reads as ~90% of the theme background colour over the wallpaper, via
-- Theme.windowAlpha in QML). Compositor-level opacity multiplies on top of window content,
-- so leaving the tag in place would double the effect (the ground would read closer to 86%,
-- not 90%) and any fully-opaque chrome (menus, the tray icon, notifications) would also pick
-- up an unwanted 1.5-4% translucency. omaseed is the only source of its own translucency:
-- drop the tag and force the window itself to full opacity.
o.window(class, { tag = "-default-opacity", opacity = "1 1" })

-- First-run window: float, centre, fixed size.
--
-- T4.6's first-run detector titles its window exactly "omaseed · first run" while that page
-- is showing (and only then -- this is the contract other tasks should honour: match on
-- this literal title, restore the normal float/tile behaviour once the title changes back).
-- The wizard is short and not meant to be tiled or resized, so float+centre+size it at a
-- size that comfortably fits the spec's server-add and library-detector steps.
o.window(
  { class = class, title = "^omaseed · first run$" },
  { float = true, center = true, size = { 1360, 800 } }
)
