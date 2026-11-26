# Engines and servers

omaseed has no torrent engine of its own. It drives qBittorrent, Transmission or Deluge, on this
machine or another, through their public APIs. Back to the [README](../README.md).

## What each engine needs

- **qBittorrent** 5.2 or newer with its Web UI turned on (`Tools → Options → Web UI` in the
  desktop app, or `qbittorrent-nox`). omaseed signs in with an API key, a username and
  password, or nothing for a server that lets localhost in. It never installs qBittorrent
  itself, except when you accept first run's offer (README, First run).
- **Transmission** 4.1 or newer with remote access on (`transmission-daemon`, or the desktop
  app's Preferences › Remote), signed in with a username and password or none. An older
  daemon is refused with words that say why: 4.1 changed its RPC.
- **Deluge** 2.1 or newer through its web UI (`deluge-web`, signed in with its password) with
  a daemon (`deluged`) behind it. When deluge-web drives no daemon, or knows several, the
  server sheet asks which one to use; connecting one switches deluge-web for everyone who
  uses it, so omaseed only ever connects one when deluge-web has none.

## What omaseed does on each

On qBittorrent, everything the README describes.

omaseed does everything on Transmission that Transmission can do over its RPC: the library,
details, adding (magnets, `.torrent` files, labels, a folder), renaming, moving, labels,
limits and turtle with its clock, errors in its own words, the seed console, and Settings ›
Transmission. What Transmission has no way to do is left out rather than offered and
failing, and its key says so once in the status line: categories, a magnet's files before
the add, super seeding, first and last pieces first, automatic management, adding or
banning peers, editing web seeds, the log face, search, creating a torrent, and
qBittorrent's settings export and import. Its torrents do move: Settings › Transmission ›
Backup exports them to a file and imports a file exported on any engine.

The same goes for Deluge: the library, details, adding (magnets with their files read before
the add, `.torrent` files, a label, a folder), renaming (the name Deluge shows; the folder
keeps its own), moving with Deluge's own percent, labels from its Label plugin (in the
categories' place, lower case), ratio limits that stop or remove, errors in its own words,
the seed console (counted from when omaseed began watching: Deluge keeps no all-time
totals), and Settings › Deluge with its Plugins. A move Deluge fails pauses the torrent until
a recheck, so recheck comes first; a move it takes and drops without a word reads as failed.
Left out: tags, turtle, time limits, automatic management, banning peers, web seeds, the log
face, search, creating a torrent, and qBittorrent's settings export and import. Its torrents
move too: Settings › Deluge › Backup exports them to a file and imports a file exported on
any engine, keeping one label per torrent (Deluge has one) and saying what is left behind.

## Servers

Settings › Servers lists every server, with **add**, **test**, **edit**, **remove** and
**set as default**. Switch between them from the rail's Servers group or here; a right-click
on a server in the rail opens its menu. Passwords
and API keys go to the system keyring through libsecret, or to a 0600 file under
`~/.config/omaseed/secrets/` when there is no keyring.

For a server whose files appear under another path here (a NAS mount, a seedbox), Settings ›
General's **path mapping** rewrites the server's save path before open folder.

Settings › qBittorrent edits the server's own settings (downloads, connection, speed,
BitTorrent, RSS, Web UI, advanced) without opening its web UI; Settings › Transmission does
the same for a Transmission (speed and turtle, discovery and privacy, connection, seeding,
queue, downloads), and Settings › Deluge for a Deluge (the same sections from its core.conf,
then which of its plugins are on). Each engine with a saved server keeps its group in Settings.
