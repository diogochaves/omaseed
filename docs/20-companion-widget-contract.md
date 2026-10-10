# Companion bar widget — build contract

Status: **settled 2026-10-02.** Every decision below is Diogo's, across Q6–Q12 and four rounds of
`docs/19-companion-widget.html` (artifact https://claude.ai/artifact/5MGszxU3UmZutosjtfLupX). This
is what Phase 6 (#12–#17) builds. It is built before v0.1.0, and **nothing starts until Diogo says
go.**

How the documents relate:

- **This file wins** wherever it differs from the September design.
- `docs/07-companion-widget-brief.md` still holds for the purpose and boundary (§1), the status
  file's mechanism and cadence (§4, spec Fig. 36/36b), the abbreviation rules (§2), and the
  plugin's file layout (§6). Everything else there is superseded here.
- `docs/07-companion-widget-spec.html` is history.
- `docs/19-companion-widget.html` is the current drawing.

## 1. Settled rulings

| # | Ruling |
|---|---|
| Q6 | The popup keeps "in flight". |
| Q7 | Tray hand-off by heartbeat: the widget touches `$XDG_RUNTIME_DIR/omaseed/widget.alive` every 30 s, and the app hides its tray icon while that file is under 90 s old. This uses the app's existing `trayIconMode` (auto · always · never, default auto). |
| Q8 | Plugin id `chaves.omaseed`. |
| Q9 | Middle click defaults to alt-speed. |
| Q10 → C4 | Foreground, accent and urgent come from the shell. **Blue, green and yellow come from the theme's `colors.toml`**, as the app reads them (revisited 2026-10-02). |
| Q11 | ~~No settings view in the popup.~~ Revised 2026-10-03 (docs/21): the shell draws no manifest form, so the gear opens the widget's own settings page; its last row opens omaseed's settings. |
| Q12 | No status-schema compatibility. An unknown `v` reads as "app not running". |
| Timing | Built before v0.1.0, and ships with it. Not started until Diogo says go. |
| Install | Ruled after the build (2026-10-02): **the widget is part of the install**, not a separate repository. The package ships it at `/usr/share/omaseed/plugin`; first run offers it (a fourth step, "bar") and Settings › Appearance switches it; both link `~/.config/omarchy/plugins/chaves.omaseed` to the packaged folder, so app and widget always upgrade together. The `omaseed-bar` repository and the marketplace listing are parked. |
| C1 | The bar shows **the mark only** out of the box. |
| C2 | The mark's lit arrows default to **blue in, green out**. |
| C3 | Sections open the first time: **Add, In flight**. Folded the first time: Your share, Keeping alive, Lately. Afterwards the widget remembers how they were left. |
| B2 | "Your share" counts **this session** (since omaseed started). |
| N2 | Popup wording follows **the shell's card style**: Title Case buttons, UPPERCASE tracked captions. |
| Features | All seeder features go in the first build: I1–I7 (§4). |
| Round 4 | Add has no text field. "Open omaseed" is a head button. No start/stop buttons. Drag and drop is out (tested, §6). Paste anywhere is in (tested). |

## 2. The bar

- **The mark is the activity readout.** The exchange mark's three parts light independently.
  This is `Kit/Icon.qml`'s `mark` path split in three:
  - the lower-left arrow `M1 8 H6 V18 H16 V23 H1 Z` lights while downloading;
  - the upper-right arrow `M8 1 H23 V16 H18 V6 H8 Z` lights while uploading;
  - the square `M9 9 H15 V15 H9 Z` carries the link.
- **Colours.**
  - A lit arrow is the theme's blue (in) or green (out) under `markColor: directions`, the
    default, or the accent under `single`.
  - An unlit arrow is foreground at 30 %.
- **The square:**
  - foreground while the link is fine;
  - **an outline in the alt-speed colour** (the theme's yellow, the app's alt-speed chip)
    while alternative limits are on;
  - urgent with torrents in error;
  - an urgent outline when the server is unreachable.
- **App not running:** the whole mark at 45 %.
- **Two independent switches:** `showMark` (default on) and `showSpeed` (default off). Either or
  both may be on, never neither. The form refuses to turn off the last one.
- **`speedMetric`:** down · up · both (default both).
  - In a vertical bar, the speed stacks under the mark: an arrow line, then a four-character
    value per direction.
  - In a horizontal bar, it's a five-character tabular field per direction, beside the mark.
  - A direction with nothing moving shows `0`. With nothing to report (unreachable, not
    running) there is no value at all.
  - Under alt-speed the values take the alt-speed colour.
- **Width:** fixed, measured once with `FontMetrics` (the omagauge idiom), so the bar never
  reflows. In Diogo's vertical bar (42 px at base 14) the widget stacks like omagauge.
- **Open popup:** marked with the shell's own accent line along the bar.
- **Tooltip:**
  - Line 1: `home · ↓ … ↑ … · N downloading · N seeding`.
  - Line 2: what you gave this session and the ratio.
  - A news line only when there is news: alt-speed on, unreachable with the reason, or not
    running.
  - The click map last.
- **Clicks:**
  - left toggles the popup;
  - middle: `middleClick` altSpeed (default) · openApp · none;
  - right: `rightClick` openApp (default) · altSpeed · none;
  - scroll does nothing.
- **`hideWhenIdle`** (default off): the widget leaves the bar while nothing moves. An
  unreachable server still shows.

## 3. The popup

`Ui.Panel` opening a `Ui.KeyboardPanel`, `Style.space(380)` wide, opening beside the bar level with
the widget, like chaves.omagauge. Shell card style throughout (§1 N2).

**Head** (never folds):
- the live mark, then the title "omaseed ↗", a button that opens the app;
- the server line with its dot (`HOME · QBITTORRENT 5.2.3`);
- three icon buttons:
  - **alt-speed**: Kit/Icon's gauge, in the alt-speed colour while on;
  - **open omaseed**: the mark;
  - **settings**: the gear, which opens the widget's settings page (docs/21); omaseed's own settings are its last row.

**Now** (never folds):
- ↓ and ↑ as heading numbers over tracked captions, the arrows blue and green.
- Under alt-speed, both numbers and arrows take the alt-speed colour and one line names the
  limits.
- An upload sparkline for the last hour: 60 one-minute points sampled by the app, with peak
  and current. It hides when nothing has been uploaded for an hour.

**Sections:** each folds by its header or `space`, keeps one summary line while folded, and
remembers its state. Order:

1. **Add** (open the first time). No text field. It lists what is ready to add:
   - what was pasted (`ctrl+v` anywhere in the open popup);
   - the magnet or link on the clipboard when the popup opened (`wl-paste -n -t text`);
   - the `.torrent` files that landed in `~/Downloads` today.

   Each row reads name · origin/size/age. A duplicate reads "already on <server>" and stays
   unticked. Below the rows:
   - category chips, from the server, with the target folder and whether automatic management
     applies;
   - **Add N** (primary) and **Browse…** (the system picker via
     `GTK_USE_PORTAL=1 zenity --file-selection --multiple`).

   With nothing ready: Browse… and "or copy a magnet and press ctrl+v here". A paste unfolds
   Add if it was folded.
2. **Your share** (folded). The folded line is the given amount and the ratio.
   - Given, taken and ratio this session.
   - The week strip: seven days of upload, today highlighted.
   - "N GB seeded this week · N seeding · N TB on offer · all time ratio N".
   - Today's top seeders: two rows, what each gave today and ratio (#47: from the seed
     ledger, not `uploaded_session`).
3. **Keeping alive** (folded). The folded line is "N with 2 or fewer seeds · N only you".
   - The held count.
   - The tallies: only you · you + one · peers waiting · nobody has asked in a month.
   - The three rarest rows: "only you" first, then most waiting.
4. **In flight** (open). Up to three downloads: name, percent · ETA, and a 2 px progress line.
5. **Lately** (folded). Up to `recentCount` rows: completions ("finished · size · category ·
   when"), mixed with ratio milestones ("paid back" at 1.0, then 2.0 and 5.0).

**Reachability warning** (I7): when qBittorrent reports `firewalled`, an urgent-ruled block under
the head says "Peers cannot reach you" and names the fix (forward the port or turn on UPnP, and
where).

**Keys:**

| Key | Does |
|---|---|
| `ctrl+v` | paste into Add, anywhere in the popup |
| `a` | go to Add, unfolding it |
| `b` | Browse… |
| `↵` | Add the ticked sources (on Add), open the folder (on a Lately row), show the torrent (on an In flight row) |
| `shift ↵` | Add stopped |
| `x` | tick or untick the Add row the cursor is on, as in the Files tab (#47) |
| `m` | omaseed's own add sheet with the ticked sources |
| `o` | open omaseed |
| `A` | alt-speed |
| `j k` | move the cursor |
| `space` | fold the section the cursor is on |
| `z` | fold or unfold every section |
| `s` | show the cursor's torrent in omaseed |
| `tab` | next bar widget |
| `esc` | close |

`p` and `P` are unbound: no start or stop anywhere in the widget.

**States:**
- **Unreachable:** urgent mark and dot, live numbers are —, the last known rows at 45 % and
  read-only, actions disabled rather than hidden, and `r` retries.
- **Not running** (no status file, one older than 30 s, or an unknown `v`): one sentence and
  **Start omaseed**.
- **Idle:** zeros are written `0 B/s`.

## 4. Seeder features (I1–I7) and their data

All of them come from data qBittorrent already reports (checked on the 5.2.3 playground) or from
the app's own sampling. The widget computes none of it: the app writes it into the status file.

| | Feature | Data |
|---|---|---|
| I1 | Your share this session | `server_state.up_info_data`, `dl_info_data` (already read) |
| I2 | Upload sparkline, last hour | the app samples upload each second and keeps 60 one-minute averages |
| I3 | Keeping alive | per torrent `num_complete` (seeds in the swarm) and `num_incomplete` (peers waiting), on complete torrents; "nobody has asked in a month" uses `last_activity` older than 30 days; threshold option `rareSeeds` (default 2); a torrent whose tracker reports 0 seeds or an error is unknown and not counted, the seed console's rule (#47) |
| I4 | Today's top seeders | each torrent's upload today in the seed ledger, `SeedLedger::dayTotals` (#47; was `uploaded_session`) |
| I5 | Week strip, all time | the app keeps daily upload totals in state.json; `server_state.alltime_ul`, `alltime_dl`, `global_ratio` (a string; not read yet) |
| I6 | Ratio milestones | the app notices a torrent's `ratio` crossing 1.0, 2.0 and 5.0 and keeps the last few, like completions |
| I7 | Reachability warning | `server_state.connection_status == "firewalled"` (already read) |

## 5. Contract with the app

**Read:** `$XDG_RUNTIME_DIR/omaseed/status.json`, watched with `FileView { watchChanges: true }`.

- Schema, cadence, atomicity and the rule against secrets are as in `docs/07` spec Fig. 36/36b.
- Bump `v` to 2 for these additions:
  - `categories` (name, save path, auto-TMM);
  - `share` (session up/down/ratio, all-time up/down/ratio);
  - `uploadHour` (60 numbers);
  - `week` (7 daily upload totals, oldest first);
  - `alive` (counts and the three rarest rows);
  - `topSeeders` (two rows);
  - `downloads` (today's `.torrent` files in ~/Downloads, as the app's add sheet already finds
    them);
  - `recent` (completions and milestones, each with a `kind`);
  - `reachable` (connected · firewalled · disconnected).

**Act:** `io.github.diogochaves.omaseed.Status` on the existing object path, called with
`busctl --user call` fire-and-forget. Methods only, no properties or signals.

| Method | Use |
|---|---|
| `ToggleAltSpeed() → b`, `SetAltSpeed(b) → b` | the head button, `A`, middle click |
| `DescribeSources(as) → s` | JSON per source: name, kind, size, duplicate-on-server. For pasted and browsed items before they are listed. |
| `AddSources(as sources, s category, b stopped) → s` | JSON result. The app adds, checks duplicates, remembers the category. |
| `OpenAddSheet(as)` | `m`: the app's own add sheet with these sources |
| `ShowTorrent(s hash)`, `OpenFolder(s hash)` | rows |

Launch and raise go through `org.freedesktop.Application.Activate` (DBusActivatable), or a
desktop-file launch.

**Dropped from the September design:**
- `PauseAll`/`ResumeAll`: no start/stop, and middle click no longer offers it.
- `AddFromClipboard`: replaced by `DescribeSources` + `AddSources`.

## 6. Tested, out, parked

- **Paste anywhere works** (box test, 2026-10-02): a click-opened `KeyboardPanel` with
  `focusTarget` set receives `ctrl+v` with no field focused, and `wl-paste` reads the clipboard.
- **Drag and drop onto the widget is out** (box test, Omarchy 4.0.4, Quickshell 0.3.1, Hyprland
  0.56.2):
  - a DropArea on the bar icon never hears a drag from Nautilus;
  - with the popup open, the press that starts a drag lands on its outside-click layer;
  - only a plugin's own small surface takes drops (`sridhar.shelf`).

  jankeesvw/omarchy-downloads solves drag *out* of a panel, not into one.
- **Parked:** dragging a finished download out of a "Lately" row into another app (that
  plugin's window-and-mask technique), in #7 for v2.

## 7. Options (manifest `barWidget.schema`)

| Key | Type | Default |
|---|---|---|
| `showMark` | bool | true |
| `showSpeed` | bool | false |
| `speedMetric` | enum down · up · both | both |
| `markColor` | enum directions · single | directions |
| `hideWhenIdle` | bool | false |
| `rareSeeds` | integer 1–5 | 2 |
| `recentCount` | integer 0–5 | 3 |
| `middleClick` | enum altSpeed · openApp · none | altSpeed |
| `rightClick` | enum openApp · altSpeed · none | openApp |

Fold states are remembered by the widget, not options.

## 8. Build issues

| Issue | Task | Added to its scope here |
|---|---|---|
| #12 | T6.1 StatusWriter | schema v2 (§5), the upload sampler, the daily totals, milestones, keeping-alive tallies, top seeders; parse `uploaded_session`, `last_activity`, `alltime_*`, `global_ratio` |
| #13 | T6.2 D-Bus | the method list of §5 (`DescribeSources`, `AddSources`, `OpenAddSheet` new; pause/resume dropped) |
| #14 | T6.3 tray hand-off | on the existing `trayIconMode` |
| #15 | T6.4 the plugin | §2, §3, §7; colours per §1 C4; checked in an omabox in Diogo's vertical bar and a horizontal one |
| #16 | T6.5 Format.js tests | plus the vertical four-character values |
| #17 | T6.6 publish | unchanged |
