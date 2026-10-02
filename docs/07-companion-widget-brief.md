# Companion bar widget — brief

Status: **superseded in part by `docs/20-companion-widget-contract.md` (2026-10-02), which wins where they
differ; §1, §2's abbreviation rules, §4's status-file mechanism and §6 still hold.** Approved by Diogo 2026-10-02 (Q6–Q12 ruled as proposed, plugin id `chaves.omaseed`;
built before v0.1.0, issues #12–#17). Proposed 2026-09-19. Stage 1 of the companion-widget work. The drawing
is `docs/07-companion-widget-spec.html`; this file decides what is drawn and why. Foreseen by
charter §9 ("a thin QML plugin that shows speeds in the bar and summons the app over D-Bus");
this turns that sentence into a contract. Nothing here edits the approved specs
(`docs/04-ui-spec.html`, `docs/05-ui-spec-v2.html`) — they stay frozen.

Everything is a decision, not an option, unless it says **[decide]**.

---

## 1. Purpose and boundary

**The widget is a mirror with five switches.** It shows what omaseed already knows, in the
bar, and it performs the handful of actions that are not worth raising a window for. It never
computes anything omaseed does not already compute, and it never talks to qBittorrent.

### What a torrent user actually does in a day, ranked

| # | Task | How often | Needs a window? |
|---|---|---|---|
| 1 | Glance: is anything moving, how fast, is the server alive | dozens of times | no |
| 2 | Throttle: "someone is on a call / I am gaming" → alternative speed limits | 1–5× a day | no |
| 3 | Catch up: what finished while I was away, and where did it land | 1–3× a day | no |
| 4 | Open the folder of something that just finished | 1–3× a day | no |
| 5 | Add a magnet the browser did not hand over (copied as text) | a few a week | no |
| 6 | Stop / start everything (leaving the house, saving a flaky link) | a few a week | no |
| 7 | Jump into the app on a particular torrent | a few a week | **it is the jump** |
| 8 | Anything with a decision in it: delete, recheck, category, limits, files, trackers | weekly | **yes** |

1–7 are the widget. 8 is the app, and the widget's job for 8 is to get you there in one key.

### v1 set — in

- **Glance**: download and upload speed, counts by state, free space, server name and a
  reachability dot, alt-speed indicator. Bar and popup.
- **Alt-speed toggle**: the single most-wanted switch, and the civilised alternative to
  pausing. Reachable without opening the popup (middle click).
- **Stop all / start all**: two buttons in the popup only. Never on a click, never on scroll —
  see §2.
- **Recent completions**: the last few, with *open folder* and *show in omaseed*. This is the
  reason the popup exists rather than just a tooltip: a notification you missed is gone, a
  completion list is not.
- **In flight**: up to three active downloads, name + progress line + ETA, read-only. Without
  it the popup is numbers you already read in the bar.
- **Add from clipboard**: enabled only when the clipboard holds a `magnet:` or a `.torrent`
  URL; hands it to the app, which opens its own add sheet. The widget never shows add options.
- **Launch / raise omaseed**, including a first-class "omaseed is not running" state with one
  button that starts it.

### v1 set — out, on purpose

- **No table.** No columns, no sorting, no selection, no bulk. The in-flight and completions
  lists are capped at 3 and 5 rows and have a cursor, not a selection model.
- **No details.** No files, trackers, peers, hashes, ratios, or per-torrent editing.
- **No add options.** No category, tags, save path, or paused-on-add. The app owns the sheet.
- **No delete.** Nothing destructive and irreversible happens behind a bar popup.
- **No settings, no servers, no first-run.** The app owns configuration; the widget has six
  display options (§5) and nothing else.
- **No search or filter.** If you are looking for a torrent, you want the app (`ctrl+k`).
- **No quit.** Quitting a resident app from the bar is a footgun; it lives in the app and in
  the tray menu (spec §6 Fig. 18).
- **No second client.** The widget does not know qBittorrent's API exists (§4).

The one item that could reasonably be cut is **in flight**; if Diogo wants the popup smaller,
that section goes first and the rest still stands. **Ruled 2026-10-02 (Q6): keep.**

### What this replaces, and what it fixes

`marcuspelo.omarqui` is the bar plugin omaseed replaces (charter §1). It is a good plugin and it
proves the appetite, but every one of its limits is structural and follows from having no app
behind it:

| omarqui | this widget |
|---|---|
| needs a separate **Qui** server; cannot talk to a bare qBittorrent | talks only to omaseed, which talks to qBittorrent |
| an API key in `~/.config/omarqui/.env`, not editable from the panel, manual first-run | **no credential of any kind** in the shell |
| polls every 5–300 s with 3–4 forked `curl` processes per tick; bar speeds lag by the interval | one inotify-watched file; **no polling, no subprocess** at rest |
| refetches 500 torrents wholesale and restores scroll around the swap | reads six numbers and eight rows |
| hardcoded `#7aa2f7` / `#8fd694` with a manual luminance flip | `Style` and `Color` tokens only |
| no global speed-limit toggle | alt-speed is the headline switch |
| manual `pathMap` string for *open folder* on remote paths | the app maps the path and hands over the local one |
| loses `baseUrl` when disabled and re-enabled | nothing to lose: all state is the app's |
| 2 559 lines in one `Panel.qml` | `Widget.qml` + `Popup.qml` + `Service.qml` + `Format.js` |

That table is the argument for the whole design: **the widget is thin because the app is real.**

---

## 2. Presence in the bar

The bar is dense and it is Diogo's bar: the widget earns roughly the width of the clock or
less, and never reflows.

### Collapsed form

Exactly the tray form of spec §6 Fig. 17: the mark `◆`, then one speed.

```
◆ ↓4.2M
```

The mark is the same `◆` as the app's hero and tray, at `Style.bar.iconFont` in a
`Style.bar.iconSlot` square, so it sits on the same baseline as every other bar glyph. The
number is `Style.font.caption`, tabular, right-aligned in a **fixed five-character field**, so
the widget's width is constant from `0` to `999M` and the bar never jumps.

The chip is a `Ui.WidgetButton` with `fixedWidth` computed from `FontMetrics.advanceWidth` over
the widest string the slot can ever hold — the pre-measuring idiom `chaves.omagauge` uses so its
pill never reflows — and the content is a `Grid { columns: vertical ? 1 : 2 }` so the same
children serve a horizontal and a vertical bar.

### State table

The shell's palette has five roles only — `Color.foreground`, `Color.background`,
`Color.accent`, `Color.urgent`, `Color.muted`. There is no green, blue or yellow to borrow, so
the app's semantic state colours (spec §8) collapse to three treatments here. That is a
deliberate narrowing, not an omission.

| State | Mark | Numbers | Treatment |
|---|---|---|---|
| app not running | `◆` | none | foreground at 45 % |
| server unreachable | `◆` | none | `Color.muted`; tooltip carries the reason |
| error (torrents in error) | `◆` | current speeds | `Color.urgent` |
| idle (connected, nothing moving) | `◆` | none | `Color.bar.text`; whole widget hidden if `hideWhenIdle` |
| downloading | `◆` | `↓4.2M` | mark `Color.accent`, numbers `Color.bar.text` |
| seeding only | `◆` | `↑812K` | as idle, upload number shown even when `showUpload` is off |
| alt-speed on | `◆` | `⏷↓1.0M` | mark `Color.accent`, `⏷` prefix on the number |

`showUpload` (off by default) appends a second five-character field: `◆ ↓4.2M ↑812K`.

### Abbreviation rules (one rule, everywhere in the widget)

- Bytes per second, one letter, no `/s`: `K`, `M`, `G`. `1024`-based, matching the app.
- Under 10: one decimal — `4.2M`, `1.1G`. Ten and over: no decimal — `842K`, `112M`.
- Under 1 KB/s: `0`. Never `0.0K`, never `—` for a live zero.
- Unknown (disconnected, app not running): the field is empty, not `—`. `—` is for the popup,
  where there is room to be explicit.
- The popup uses full units (`4.21 MB/s`), the bar uses the short form. Same numbers.

### Width discipline

Max width, base 12: `iconSlot` (27) + `xs` (3) + 5 ch caption (~30) = **~60 px** download-only,
**~95 px** with upload. Nothing in the widget is allowed to grow past that: no server name, no
counts, no category, no torrent name. Everything else is a hover or a click away.

### Hover

`WidgetButton.tooltipText` (which routes to the host's `bar.showTooltip`), carrying spec §6's
line verbatim:

```
home · ↓ 4.21 MB/s ↑ 812 KB/s · 6 downloading · 118 seeding
```

A second line appears only when it is news: `alternative speed limits on`, or
`unreachable · connection refused · retrying every 10 s`, or `omaseed is not running`. The
third line is the click legend, the way `chaves.sysmon` writes it: `L: panel  M: alt-speed
R: open omaseed`.

### Pointer and scroll

| Input | Action | Why |
|---|---|---|
| left click | toggle the popup | the only discoverable gesture |
| middle click | toggle alt-speed | same as the tray (spec §6), and the action people repeat |
| right click | raise omaseed (`Activate`) | the fast path into the app |
| scroll | **nothing** | a scroll over a speed readout invites "set a global limit by accident"; a throttle that changes when you brush the bar is worse than no throttle |

Middle and right are configurable (§5) because they are the two people argue about.

### Coexistence with the SNI tray icon (T1.5)

**When the widget is installed and running, omaseed hides its tray icon by default.** Two `◆`
in one bar — one from `omarchy.tray`, one from the plugin — is the smell the task names, and
the widget strictly dominates: it has the same numbers, a better popup, and it survives the app
being closed.

Mechanism, chosen so neither side has to know the other's internals: the widget touches
`$XDG_RUNTIME_DIR/omaseed/widget.alive` every 30 s; omaseed watches that directory (it already
watches it — it writes the status file there) and hides the SNI while the file's mtime is under
90 s old. Remove the widget and the tray icon is back within 90 s. The app exposes
`Settings → Appearance → Tray icon: auto | always | never`, default `auto`; `always` is the
escape hatch for anyone who wants both. **Ruled 2026-10-02 (Q7): heartbeat.** The alternative is no heartbeat and a
plain manual toggle, which is less magic and more friction.

---

## 3. The popup

### Which shell kind

`kinds: ["bar-widget"]`, one entry point `entryPoints.barWidget: "Widget.qml"`, extending
`Ui.Panel` and opening a `Ui.KeyboardPanel` (which is the shell's `PopupCard` plus focus and
cursor handling). That is exactly the first-party shape — `omarchy.audio`, `omarchy.bluetooth`
and `omarchy.dropbox` all declare `bar-widget` and point `barWidget` at a `Panel.qml` — and the
shape every `chaves.*` plugin on this bar already uses. The `panel` kind is for surfaces
summoned without a bar presence (`omarchy.disk-speedtest`); we have a bar presence, so it is the
wrong kind. `Ui.Panel` also brings an `IpcHandler` for free, so the popup can be bound to a
Hyprland key.

Following the same family: parsing and formatting live in a plain `Format.js`, not in QML; the
service logic (the `FileView`, the `Process` objects, the derived state) lives in a
`Service.qml` instantiated as a child of `Widget.qml`, not as a host-managed `service` entry
point — the omartx / omawin / omaplex arrangement. `marcuspelo.omarqui`'s 2 559-line single
`Panel.qml` is the counter-example.

### Sizes and tokens

Everything comes from `Style` and `Color`; no literal sizes, no hex, exactly as the app's §3
rules demand of QML — and unlike omarqui, which hardcodes `#7aa2f7` / `#8fd694` for its arrows
and is the one place the family's token discipline is broken.

| Thing | Token |
|---|---|
| content width | `panel.fittedContentWidth(Style.space(380))` — the house default (omagauge, omartx, omawin, omascreen all use 380) |
| content height | `panel.fittedContentHeight(column.implicitHeight, Style.space(560))` |
| padding | `Style.spacing.popupPadding` (14) |
| section gap | `Style.space(12)` between sections, `Style.space(14)` inside one |
| row height | `Style.spacing.popupRowHeight` (28) |
| button height | `Style.spacing.controlHeight` (28) |
| border | `Color.popups.border` (accent), 1 px, `Style.cornerRadius` |
| background | `Color.popups.background` |
| hairline | `Ui.PanelSeparator`, `Color.foreground` at **12 %** |
| fills | `Style.normalFill` 4 %, `Style.hoverFill` 8 %, `Style.selectedFill` 18 %, `Style.pressedFill` 22 % |
| borders | `Style.normalBorderColor` 40 %, `Style.hoverBorderColor` 25 % |
| type | `Style.font.caption / bodySmall / body / subtitle / heading` |

Colour idiom, verbatim from the family: read off the injected `bar` first with the `Color`
singleton as fallback — `bar ? bar.foreground : Color.foreground`, `bar ? bar.urgent :
Color.urgent`, `bar ? bar.fontFamily : Style.font.family`. **In the bar** use
`bar.barForeground`, not `bar.foreground`, so the widget stays legible when the bar goes
transparent, and dim by alpha (`0.6`), not by `Qt.darker`. **In the popup** dim is
`Qt.darker(foreground, 1.55)`. Every colour transition is gated:
`Behavior on color { enabled: !bar || bar.foregroundAnimationEnabled; ColorAnimation { duration: 160 } }`.
Accent is reserved for "the popup is open" and "the cursor is here" — and, here, for the one
extra meaning the widget must carry: download activity.

### Sections, in order

1. **Head** — the `◆` mark in an accent-outlined square, `omaseed` at `Style.font.subtitle`,
   and under it the tracked caption with the state dot: `● home · qBittorrent 5.2.3`.
   *Mirrors v2 Fig. 1 `.hero .mark` + `.hero .name`.*
2. **Hero speeds** — two blocks side by side, value at `Style.font.heading` with the `↓` / `↑`
   glyph at body size, tracked caption labels `download` / `upload`. Down glyph
   `Color.accent`; up glyph `Color.foreground` (no green in the shell palette).
   *Mirrors v2 Fig. 1 `.hero .stat`.*
3. **Status line** — `6 downloading · 118 seeding · 12 stopped · 1.8 TB free` at
   `bodySmall`, dim. When alt-speed is on, a second accent line: `⏷ alternative speed limits`.
   *Mirrors v2 Fig. 18's two dim menu rows.*
4. **In flight** (≤ 3) — name elided, a 2 px progress line on a 12 % track, percent and ETA
   right-aligned at caption. Read-only; the cursor can sit on a row and `enter` jumps into the
   app on it. *Mirrors v2 Fig. 1 `.tbl .pg`.*
5. **Quick actions** — one row of outlined buttons at 28 px: `⏷ alt-speed` (an on/off outline,
   accent when on), `■ stop all`, `▶ start all`, `+ add magnet`. The last is enabled only when
   the clipboard holds a magnet or `.torrent` URL, and its label then reads the host name.
   Primaries are accent **outlines**, never fills. *Mirrors v2 Fig. 18's action rows and the
   control states of v2 §8 (delta 10).*
6. **Recent completions** (≤ `recentCount`, default 3) — `✓`, name elided, caption meta
   `1.31 GB · linux · 12 min ago`. Cursor row at 8 % fill. *The meta line is the body format of
   v2 Fig. 19 (`name · size · category`), so a completion reads the same in the toast and in
   the list.*
7. **Footer key hints** — unboxed, keys bright, words dim.
   *Mirrors v2 Fig. 1's footer and delta 05: no boxed keys anywhere.*
   The shell kit has **no key-hint component** (I grepped; there is none), so this is one
   `Text` at `Style.font.caption` in the dim colour, the way `chaves.omagauge` ends its card:
   `j k move · ↵ folder · s show · A alt-speed · o omaseed · esc close`. It **wraps** to a
   second line rather than eliding — at 380 px it does not fit on one, and a hidden `esc`
   teaches nothing.
   That is also why the brief does not promise the app's two-part "facts left, help right"
   footer here: at 380 px there is room for one line, and it should be the keys.

`Ui.PanelSeparator` (12 % hairline) separates head/hero, status/in-flight, actions/completions
and completions/footer. Four hairlines, no boxes.

### Keyboard

`Ui.PanelKeyCatcher` wraps the content; one cursor walks the in-flight rows then the
completions rows. The letters are **the app's letters** (spec §7 Fig. 26), so the muscle memory
carries:

| Key | Action |
|---|---|
| `j` `k` / `↓` `↑` | move the cursor |
| `enter` | on a completion: open its folder · on an in-flight row: show it in omaseed |
| `s` | show the cursor row in omaseed (`ShowTorrent`) |
| `o` | open omaseed (raise) |
| `A` | toggle alternative speed limits |
| `p` / `P` | stop all / start all |
| `a` | add from clipboard |
| `esc` | close |

`PanelKeyCatcher` emits `moveRequested`, `activateRequested`, `returnRequested`,
`closeRequested`, `deleteRequested`, `tabRequested` and `textKey`; the widget maps four of them
to the table above, wires `tabRequested` to `switchPanel(direction)` so `tab` hands the popout
to the next bar widget like every other panel, and **leaves `deleteRequested` unbound** —
`x`/`X` is the shell-wide destructive key (forget network, unpair device) and the widget has
nothing destructive to offer. Every key in the table is in the footer line, so the popup
teaches itself, the way v2 Fig. 22 does.

The panel takes keyboard focus through `KeyboardPanel.focusTarget: keyCatcher`; that is what
makes a keybind summon usable, and it is the reason for `KeyboardPanel` over `PopupCard` (an
xdg-popup only receives keys after a click has routed focus through its parent).

### Other states

| State | Popup |
|---|---|
| **idle** | hero reads `↓ 0 B/s` / `↑ 0 B/s` (honest, not `—`); in-flight collapses to one dim line `nothing in flight`; completions stay |
| **alt-speed on** | accent line under the status line, toggle on, `⏷` prefixes the hero download value |
| **disconnected** | hero `—`, dot `Color.urgent`, a one-line banner with a 2 px urgent left rule: `connection refused · retrying in 7 s` and a `retry` accent-text action; the last known counts stay at 45 %; quick actions disabled. *Mirrors v2 Fig. 23's banner and Fig. 21's notice.* |
| **app not running** | the body is replaced by a teaching empty state: `omaseed is not running`, a dim line `the widget shows what the app reports`, one accent-outline primary `start omaseed`, hint `enter start`. No stale numbers. *Mirrors v2 Fig. 22.* |
| **no completions yet** | the section is absent, not an empty box |

---

## 4. Data and integration contract

### The decision

**Read through a watched JSON file. Act through D-Bus. Launch through
`org.freedesktop.Application`.** The split is not a compromise, it is what the host actually
offers.

Quickshell exposes **no generic D-Bus client type to QML**. `Quickshell.DBusMenu` and the
`Quickshell.Services.*` singletons (SystemTray, Mpris, UPower, Notifications…) are purpose-built
C++ types; there is no `DBusObject` / `DBusProperties` a plugin can point at an arbitrary bus
name. Reading properties from QML therefore means spawning `busctl --json=short get-property`
on a timer — one process per tick for a widget that updates every second. That is exactly the
cost a bar widget must not impose on the shell.

`Quickshell.Io.FileView` gives the opposite: `path`, `watchChanges: true`, a JSON adapter, and
inotify-driven updates with no polling and no subprocess. The shell reads its own theme and
config this way (`Commons/Color.qml` holds three `FileView`s for `colors.toml` and
`shell.toml`), so a watched file is the house idiom, not a workaround.

Actions are the mirror image: they happen a handful of times a day, and one short-lived
`busctl --user call` per click costs nothing. So the interface carries **methods only** — no
properties, no signals. The app then has exactly one representation of its state (the file) and
one way to be told to do something (the bus), instead of two of each.

### Read path — `$XDG_RUNTIME_DIR/omaseed/status.json`

Directory `0700`, file `0600`, written **atomically** (`status.json.tmp` → `rename(2)`; a
watcher never sees a half file). Deleted on clean quit. `$XDG_RUNTIME_DIR` is wiped at logout,
so nothing survives a session.

```jsonc
{
  "v": 1,                          // schema version, integer, bumped on breaking change
  "t": 1789346626,                 // unix seconds, stamped on every write
  "pid": 48213,
  "app":    { "version": "1.0.0", "window": "hidden" },        // "shown" | "hidden"
  "server": { "name": "home", "url": "127.0.0.1:8080", "version": "5.2.3",
              "reachable": true, "error": "" },
  "speed":  { "dl": 4415221, "up": 831488, "dlLimit": 0, "upLimit": 0 },
  "alt":    { "on": false },
  "counts": { "all": 142, "downloading": 6, "seeding": 118, "completed": 121,
              "stopped": 12, "stalled": 3, "checking": 1, "errored": 2 },
  "disk":   { "free": 1978765344768 },
  "active": [ { "hash": "3f6c9a1b…", "name": "archlinux-2026.09.01-x86_64.iso",
                "progress": 0.489, "dl": 1929379, "eta": 160 } ],
  "recent": [ { "hash": "a1b2c3d4…", "name": "archlinux-2026.09.01-x86_64.iso",
                "size": 1406000000, "category": "linux",
                "path": "/data/torrents/linux/archlinux-2026.09.01-x86_64.iso",
                "at": 1789346021 } ]
}
```

Rules:

- **Numbers are numbers.** Bytes and bytes-per-second as integers, `progress` as 0..1, `eta` in
  seconds with `-1` for unknown. Never a pre-formatted string: the widget must be free to
  abbreviate to its own width rules (§2) and the app must not guess them.
- `active` is the top **3** by download speed. `recent` is the last **5** completions, newest
  first, and it is persisted in `~/.config/omaseed/state.json` so the widget is useful straight
  after an app restart.
- `recent[].path` is the **local** path, after `PathMap` has rewritten a remote server's path.
  When it cannot be mapped it is `""` and the widget disables *open folder* for that row.
- `server.error` is a short human phrase (`connection refused`, `401 unauthorised`), never a
  stack trace, never a URL with a credential in it. No API key, cookie or password is ever
  written to this file.

**Cadence.** Written on change, rate-limited to at most **1 Hz while anything is moving** and at
most **once per 10 s when idle** (the idle write is the heartbeat). Speeds are rounded to three
significant figures before the app decides whether anything changed, so a quiet session writes
about six times a minute.

**Staleness.** The widget treats the file as dead when it is absent, or when `now - t > 30 s`,
or when `v` is a version it does not understand. Dead → the "app not running" state of §3. A
crashed app therefore degrades to that state within 30 s without anyone cleaning up.

### Act path — `io.github.diogochaves.omaseed.Status`

On the existing bus name `io.github.diogochaves.omaseed`, object
`/io/github/diogochaves/omaseed` (alongside `org.freedesktop.Application` and the `Theme`
object). Methods only:

| Method | Signature | Notes |
|---|---|---|
| `ToggleAltSpeed` | `() → b` | returns the new state |
| `SetAltSpeed` | `(b on) → b` | for a toggle that knows what it wants |
| `PauseAll` | `() → ()` | `torrents/stop` on everything |
| `ResumeAll` | `() → ()` | |
| `AddFromClipboard` | `() → s` | the app reads the clipboard itself (it already has `Xdg` clipboard detection, T1.7) and opens its add sheet; returns the URI it took, or `""` |
| `ShowTorrent` | `(s hash) → ()` | raise the window, select and reveal that torrent |
| `OpenFolder` | `(s hash) → ()` | `FileManager1.ShowItems`, the app's own path mapping and fallbacks |

The widget calls these with `bar.run("busctl --user call io.github.diogochaves.omaseed …")` —
`PluginBarApi` gives every third-party widget a `run(command)`, and `omarchy.weather` already
uses it for its right-click. Calls are fire-and-forget; the file tells the widget what happened,
one write later. That is the whole feedback loop: **act on the bus, learn from the file.** The
widget never optimistically repaints a toggle it has not seen confirmed.

Two notes on the facade. A third-party widget is handed a `PluginBarApi`, not the host `Bar`,
and its operations are scoped to its own plugin id; every call is guarded
(`if (bar && typeof bar.run === "function")`) because the facade's callbacks can be null. And
the one place the widget needs a command's *output* rather than its effect — probing whether
the clipboard holds a magnet, to enable the add button — uses a `Quickshell.Io.Process` with a
`StdioCollector` on popup open, re-armed the house way: `if (proc.running) return`,
`environment: ({ LC_ALL: "C" })`, and `timeout --kill-after=0.5s 5s` in front.

Why `AddFromClipboard` rather than the widget reading the clipboard and calling `Open(uris)`:
the app already owns clipboard magnet detection and duplicate-by-infohash checking (T1.7,
T4.2). Two implementations of "is this text a torrent" is one too many. The widget only needs to
know *whether* the clipboard looks addable to enable its button, which it can do with a cheap
`wl-paste --no-newline` on popup open.

### Launch path

`org.freedesktop.Application.Activate` on `io.github.diogochaves.omaseed` raises the window,
and — because the `.desktop` carries `DBusActivatable=true` (T1.4) — **starts the app if it is
not running**. So "start omaseed" and "raise omaseed" are the same call. In practice the widget
prefers a desktop-file launch so the process lands in the right uwsm scope:
`uwsm app -- io.github.diogochaves.omaseed.desktop`, falling back to
`gio launch …/io.github.diogochaves.omaseed.desktop`, falling back to `omaseed`.

`Open(uris)` stays available for a future drop target on the bar widget; not in v1.

### Should the widget talk to qBittorrent directly when the app is closed?

**No.** Four reasons, in order of weight:

1. **Credentials.** It would need the API key, which lives in libsecret, owned by the app. A
   bar plugin holding a qBittorrent credential in `shell.json` is exactly the shape omarqui has
   and the charter rejects; it is also the thing that would make a marketplace submission
   unreviewable.
2. **Two clients.** Auth fallback, `sync/maindata` diffing, path mapping and error handling
   would exist twice, in two languages, and would drift. The charter's whole argument for an
   app is that this logic belongs in one compiled place.
3. **Cost inside the shell.** Polling a WebUI from a bar plugin is an XHR or a subprocess per
   tick in the process that draws Diogo's entire desktop. A stall there stalls the bar.
4. **Honesty.** "omaseed is not running" is a true and actionable statement with a button under
   it. A widget that quietly works without its app is a second client pretending to be a
   widget.

The widget is the app's mirror. When there is nothing to mirror, it says so.

---

## 5. Options

Declared as `barWidget.defaults` + `barWidget.schema` in the manifest — the mechanism first-party
and third-party plugins alike use (`omarchy.dropbox` and `chaves.omagauge` are the canonical
examples) — stored inline on the widget's entry in `~/.config/omarchy/shell.json` and read with
`BarWidget.setting(key, fallback)`. Not `barWidget.settingsForm`: that names a form the Setup
app provides and is not available to third-party plugins.

| Key | Type | Default | Why it earns its place |
|---|---|---|---|
| `showUpload` | `boolean` | `false` | the bar is dense; download is the number people watch. Seeders will want both. |
| `hideWhenIdle` | `boolean` | `false` | some people want the bar to be silent when nothing is happening. Unreachable still shows — that is news. |
| `showActive` | `boolean` | `true` | turns off the in-flight list for a numbers-only popup |
| `recentCount` | `integer` 0–5 | `3` | `0` removes the completions section entirely |
| `middleClick` | `enum` `altSpeed \| pauseAll \| openApp \| none` | `altSpeed` | the one gesture people have opinions about |
| `rightClick` | `enum` `openApp \| altSpeed \| none` | `openApp` | ditto |

**Deliberately absent**: no refresh interval (the file is watched; there is nothing to poll —
this is the option every other data plugin on the bar needs and we do not), no colours or
opacity (the theme decides, always), no server picker (the app owns servers), no unit style
(one abbreviation rule, §2), no font size (`Style` decides). Six options, each one answering a
question two reasonable people answer differently.

**No settings view inside the popup**, which is a deliberate departure from `chaves.omagauge`,
`chaves.omaplex` and `omarqui`, all of which put one behind `s`. Six display options do not
earn a second view, the shell already renders `barWidget.schema` as a form, and the settings
people actually want to change — servers, notifications, path maps — belong to the app. So the
popup head's trailing control is a `PanelActionButton` `󰒓` that opens **omaseed's** settings,
not the widget's. `s` therefore stays free for *show in omaseed*. **Ruled 2026-10-02 (Q11): no
settings view.**

---

## 6. Packaging

### Where it lives

In the omaseed repo, at `plugin/`:

```
plugin/manifest.json      the manifest below
plugin/Widget.qml         bar presence, click map, tooltip; extends Ui.Panel
plugin/Popup.qml          the Ui.KeyboardPanel and its seven sections
plugin/Service.qml        the FileView, the derived state, the bar.run calls
plugin/Format.js          the abbreviation rules of §2, shared and unit-tested
plugin/README.md          install and options
plugin/LICENSE            MIT, same as the app
```

`Service.qml` is instantiated as a child of `Widget.qml` and is **not** declared in
`entryPoints`; a host-managed `"service"` kind with `keepLoaded` is for plugins that must run
with no bar presence, and a bar widget is always loaded anyway. (`chaves.omartx`,
`chaves.omawin` and `chaves.omaplex` do it this way; `chaves.sysmon` declares a real service
because its sampler must outlive the panel.) One consequence to keep in mind: a bar surface
exists per monitor, so on a two-monitor desk two `FileView`s watch the same file. That is
harmless — it is a read — but nothing in the widget may assume it is a singleton.

It lives in the app's repo because it changes with the status writer it depends on: a schema
bump in §4 is one commit touching both sides, in one history, under one licence. A separate
repo as the *source of truth* would make that a two-repo dance.

### How it reaches the shell

The shell scans exactly two directories — `$OMARCHY_PATH/shell/plugins` (first-party, not ours)
and `~/.config/omarchy/plugins/` — as `omarchy-plugin-catalog` shows. **There is no
`/usr/share/omarchy/plugins`**, so a PKGBUILD cannot install a plugin anywhere the shell will
look. Therefore two routes, one source:

1. **Marketplace / anyone** (the supported route):
   `omarchy plugin add https://github.com/diogochaves/omaseed-bar.git --enable`, which git-clones
   into `~/.config/omarchy/plugins/<id>/`; `omarchy plugin update` then pulls. On each release,
   CI does a `git subtree split` of `plugin/` and pushes it to that small public repo with a
   matching tag. One source, two distributions.
2. **Package users** (convenience): the PKGBUILD installs a read-only copy at
   `/usr/share/omaseed/plugin/`, and `omaseed --install-bar-widget` **copies** it to
   `~/.config/omarchy/plugins/<id>/` and prints the enable line. A copy, not a symlink — the
   registry refuses any symlink inside a plugin folder.

### Manifest

```json
{
  "schemaVersion": 1,
  "id": "chaves.omaseed",
  "name": "omaseed",
  "version": "1.0.0",
  "author": "Diogo Chaves",
  "license": "MIT",
  "description": "Torrent speeds, alternative speed limits and recent completions from omaseed",
  "kinds": ["bar-widget"],
  "entryPoints": { "barWidget": "Widget.qml" },
  "barWidget": {
    "displayName": "omaseed",
    "description": "Speeds, alt-speed toggle and recent completions from the omaseed app",
    "category": "Files",
    "allowMultiple": false,
    "defaultSection": "right",
    "defaults": { "showUpload": false, "hideWhenIdle": false, "showActive": true,
                  "recentCount": 3, "middleClick": "altSpeed", "rightClick": "openApp" },
    "schema": [ "…the six rows of §5…" ]
  }
}
```

The id must match `^[A-Za-z0-9][A-Za-z0-9._-]*$` and must not start `omarchy.`. `chaves.*` is
Diogo's existing publisher prefix in `~/.config/omarchy/plugins/`, so `chaves.omaseed` fits the
neighbourhood; the app's D-Bus name stays `io.github.diogochaves.omaseed`. **Ruled 2026-10-02
(Q8): `chaves.omaseed`.**

`omarchy plugin validate plugin/` runs in CI and must exit 0: it enforces `schemaVersion: 1`,
the five required fields, an entry point per declared kind, relative safe paths, no symlinks,
and a valid `barWidget.defaultSection`.

---

## 7. Consequences for the app plan

Proposed new section for `docs/06-implementation-plan.md`, replacing the line in §7 that says
"the companion bar widget (charter §9) is post-v1".

> ### Phase 6 — companion bar widget (post-v1)
>
> Goal: the shell plugin of `docs/07-companion-widget-spec.html`, and the app-side status
> surface it mirrors. Ships after v1 is on the AUR; nothing in Phases 1–5 depends on it.
> The design contract is `docs/07-companion-widget-brief.md`.
>
> - **T6.1 StatusWriter (Sonnet, after T2.3).** `integration/StatusWriter`: builds the JSON of
>   brief §4 from `GlobalState`, `TorrentModel` and `FilterCounts`; writes
>   `$XDG_RUNTIME_DIR/omaseed/status.json` atomically (temp + `rename`), directory `0700`,
>   file `0600`; rate-limited to 1 Hz moving / 10 s idle with three-significant-figure change
>   detection; removes the file on clean quit; persists `recent` (last 5 completions) in
>   `state.json`. Acceptance: unit tests with `XDG_RUNTIME_DIR` in a temp dir asserting the
>   schema, atomicity (no reader ever sees a partial file under a 1 kHz read loop), the rate
>   limit, the idle heartbeat, and that no secret ever appears in the output (grep the file for
>   the strict test server's key).
> - **T6.2 Status D-Bus interface (Sonnet, after T2.5, T6.1).** `io.github.diogochaves.omaseed.Status`
>   on the existing object path: `ToggleAltSpeed`, `SetAltSpeed`, `PauseAll`, `ResumeAll`,
>   `AddFromClipboard`, `ShowTorrent`, `OpenFolder`, with the signatures of brief §4. Acceptance:
>   `busctl --user introspect` shows the interface; a test calls every method against the :8081
>   playground and asserts the effect in `torrents/info` and in the next status file write.
> - **T6.3 Tray/widget coexistence (Sonnet, after T1.5, T6.1).** The widget heartbeat file
>   `$XDG_RUNTIME_DIR/omaseed/widget.alive`; the app hides the SNI icon while it is under 90 s
>   old; `Settings → Appearance → Tray icon: auto | always | never`, default `auto`.
>   Acceptance: touching the file hides the icon within one watcher tick; letting it go stale
>   restores it; `always` and `never` override both ways.
> - **T6.4 The plugin (Opus, after T6.1–T6.3).** `plugin/` per brief §6: manifest, `Widget.qml`
>   (bar presence, tooltip, click map, `Ui.Panel` + `Ui.PopupCard`), `Popup.qml` (the seven
>   sections and five states of brief §3), `Format.js` (the abbreviation rules of brief §2).
>   Only `Style` and `Color` tokens, no literal sizes, no hex. Acceptance:
>   `omarchy plugin validate plugin/` exits 0; installed into `~/.config/omarchy/plugins/` and
>   enabled on the right, it renders every state; screenshots of the bar strip and the popup in
>   Tokyo Night, Hackerman and Lupine at base 12 and 14, compared against the spec figures by
>   the orchestrator; the shell's console is clean (no QML warnings).
> - **T6.5 Format tests (Sonnet).** `qmltestrunner` over `Format.js`: every abbreviation rule
>   of brief §2 (boundaries at 0, 999, 1000, 1023, 1024, 10 M, 999 M, 1 G), ETA and relative
>   time, byte sizes, and the fixed-width guarantee (every output is exactly five characters).
> - **T6.6 Publish (Sonnet, after T6.4).** `git subtree split` of `plugin/` pushed to the public
>   `omaseed-bar` repo on release; PKGBUILD installs `/usr/share/omaseed/plugin/`;
>   `omaseed --install-bar-widget` copies it into `~/.config/omarchy/plugins/` and prints the
>   enable line; marketplace submission per charter §9. Acceptance:
>   `omarchy plugin add <url> --enable` works on a clean checkout of the published repo.
>
> Phase 6 exit: the widget in Diogo's bar for a day with the SNI icon gone, a screenshot of every
> state, and `omarchy plugin add` from the public repo on a second machine or a clean profile.

Two smaller consequences outside Phase 6:

- **T2.3** should emit the completion event with the fields `recent[]` needs (hash, name, size,
  category, mapped path, timestamp) rather than just a hash, so `StatusWriter` does not re-read
  the model.
- **T4.5** gains one Appearance row (tray icon: auto/always/never) and one About row (the
  `omarchy plugin add` line, copyable), which is cheap if it is known now.

---

## 8. Open questions for Diogo

Collected here rather than written into `docs/QUESTIONS.md`, which the orchestrator owns.

1. **In flight in or out?** (§1) It is the section that makes the popup more than a tooltip, and
   the section closest to "a table". Keep, or cut and keep the popup to five sections?
2. **Tray heartbeat, or a plain setting?** (§2) Auto-hide the SNI icon when the widget is alive,
   or no magic and a switch in Settings the user flips themselves?
3. **Plugin id**: `chaves.omaseed` (matches your other plugins) or `diogochaves.omaseed`
   (matches the app id)? Which publisher handle will the marketplace entry use?
4. **Middle click default**: alt-speed (proposed, matches the tray) or stop-all?
5. **Colour narrowing** (§2): the shell gives five roles, so downloading, seeding, checking and
   queued all read as plain foreground and only errors get `urgent`. Acceptable, or should the
   widget read `colors.toml` itself to recover the app's semantic colours? (It can — `Color`
   already resolves the theme — but it would be the only plugin on the bar using colours the
   shell does not sanction, which is the one discipline `omarqui` breaks.)
6. **Settings view in the popup?** (§5) Your other plugins all have one behind `s`. Proposed:
   no — six options in the shell's own form, and `󰒓` opens omaseed's settings instead.
7. **Status file schema `v: 1` — do we owe compatibility?** Proposed: no. The widget refuses a
   `v` it does not know and shows "app not running"; app and plugin are versioned together and
   released together, so a mismatch is a packaging bug, not a supported state.
