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

```
omarchy plugin add https://github.com/diogochaves/omaseed-bar.git --enable
```

With the omaseed package installed, `omaseed --install-bar-widget` copies the copy it
ships into `~/.config/omarchy/plugins/chaves.omaseed/` and prints the line that enables it.

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
| `s` | show the torrent in omaseed |
| `r` | retry, while the server is unreachable |
| `tab` | the next bar widget |
| `esc` | close |

`omarchy-shell chaves.omaseed toggle` opens it from a key binding.

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
