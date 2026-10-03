# omaseed in the bar (`chaves.omaseed`)

An Omarchy bar widget for [omaseed](https://github.com/diogochaves/omaseed), the native
qBittorrent client for Omarchy. omaseed's mark is the activity readout: the lower arrow
lights while something downloads, the upper one while something uploads, and the square
says how the link is. A click opens a popup with the speeds, an hour of upload, adding
torrents, your share, the torrents you keep alive, what is downloading and what finished.

The widget is a mirror. It reads the status file omaseed writes and asks omaseed to act
over D-Bus. It never talks to qBittorrent and holds no credential. When omaseed is not
running it says so and offers to start it.

## Install

The widget comes with omaseed: the package installs it at `/usr/share/omaseed/plugin/`.
omaseed's first run offers to put it in the bar, and Settings › Appearance › Bar widget
switches it on and off. From a terminal:

```
omaseed --install-bar-widget
```

Each of these links `~/.config/omarchy/plugins/chaves.omaseed` to the installed widget and
enables it (`omarchy plugin enable chaves.omaseed`). Because it is a link, upgrading omaseed
upgrades the widget with it, so the two always agree on the status file. `omarchy update`
restarts the shell after the packages, which is when the bar picks up the new widget; after a
bare `pacman -Syu`, run `omarchy restart shell` or log in again.

While the widget runs, omaseed hides its own tray icon (Settings › Appearance › Tray icon:
auto). Remove the widget and the icon is back within 90 s.

## In the bar

| The mark | Means |
|---|---|
| lower arrow lit (blue) | downloading |
| upper arrow lit (green) | uploading |
| square outlined in yellow | alternative speed limits on |
| square in the urgent colour | torrents in error |
| square outlined in the urgent colour | the server is unreachable |
| the whole mark faint | omaseed is not running |

Blue, green and yellow are the current theme's, as omaseed uses them; the rest of the
widget follows the shell's colours.

| Click | Does |
|---|---|
| left | the popup |
| middle | alternative speed limits (option) |
| right | open omaseed (option) |

Scrolling does nothing, on purpose.

## The popup

Sections fold by their header or `space`, and stay the way you leave them. **Add** has no
text field: it lists what is ready to add (what you paste with `ctrl+v` anywhere in the
popup, a magnet on the clipboard when the popup opened, today's `.torrent` files in
~/Downloads), with category chips and **Browse…**. omaseed does the adding, checks for
duplicates and remembers the category.

| Key | Does |
|---|---|
| `ctrl+v` | paste into Add |
| `a` | go to Add |
| `b` | Browse… |
| `↵` | Add (on Add), open the folder (Lately), show the torrent (other rows), fold (headers) |
| `shift ↵` | Add stopped |
| `m` | omaseed's own add sheet with the ticked sources |
| `o` | open omaseed |
| `A` | alternative speed limits |
| `j` `k` | move |
| `space` | fold the section |
| `z` | fold or unfold every section |
| `,` | the widget's settings (the gear) |
| `s` | show the torrent in omaseed |
| `r` | retry, while the server is unreachable |
| `tab` | the next bar widget |
| `esc` | close |

`omarchy-shell chaves.omaseed toggle` opens it from a key binding.

## Settings

The gear in the popup (or `,`) opens the widget's own settings page: show the mark, show
the speed and which, the colours, hide while nothing moves, what middle and right click do,
how few seeds counts as "keeping alive", and how many rows Lately has. Each change is saved
at once in the widget's entry in `~/.config/omarchy/shell.json`, and the bar follows. The
page's last row opens omaseed's own Settings; omaseed's Settings › Appearance › Bar widget
has a **customize** button that opens this page (`omarchy-shell chaves.omaseed settings`).

From a terminal, the same settings: `omarchy bar set chaves.omaseed showSpeed true --json`.

## Options

Set in the shell's widget settings.

| Option | Default | |
|---|---|---|
| `showMark` | on | the mark as the readout |
| `showSpeed` | off | the speeds beside the mark, or under it in a vertical bar |
| `speedMetric` | both | down · up · both |
| `markColor` | directions | directions (blue in, green out) · single (the accent) |
| `hideWhenIdle` | off | leave the bar while nothing moves; an unreachable server still shows |
| `rareSeeds` | 2 | what "keeping alive" counts as few seeds (1–5) |
| `recentCount` | 3 | rows in Lately (0–5, 0 hides it) |
| `middleClick` | altSpeed | altSpeed · openApp · none |
| `rightClick` | openApp | openApp · altSpeed · none |

Mark and speed can both be on, never both off: with both off the mark shows.

## How it works

`STATUS.md` is the contract with the app: the file, its schema and its rules. The widget
watches `$XDG_RUNTIME_DIR/omaseed/status.json` (no polling), and treats a missing file, one
older than 30 s, or another schema version as "not running". It acts through
`io.github.diogochaves.omaseed.Status` and raises the app through
`org.freedesktop.Application`. It touches `$XDG_RUNTIME_DIR/omaseed/widget.alive` every
30 s so omaseed knows it is there.

## License

MIT, like omaseed.
