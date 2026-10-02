# The status file, schema v2

The contract between omaseed (the writer, `src/integration/StatusWriter`) and the bar widget
`chaves.omaseed` (the reader, `Service.qml`). Both live in this repository and change together:
a schema change is one commit touching both sides. Design: `docs/20-companion-widget-contract.md`
§4–§5; mechanism and cadence: `docs/07-companion-widget-brief.md` §4.

## Where and how

- `$XDG_RUNTIME_DIR/omaseed/status.json`. Directory `0700`, file `0600`.
- Written atomically: `status.json.tmp`, then `rename(2)`. A reader never sees half a file.
- Removed on a clean quit. `$XDG_RUNTIME_DIR` is wiped at logout.
- Written on change, at most once a second while anything moves, and at least once every 10 s
  (the heartbeat). Speeds are rounded to three significant figures before the change test.
- **No secret, ever**: no API key, password, cookie, or URL with a credential in it.
- The widget treats the file as dead (the "not running" state) when it is missing, when
  `now - t > 30`, or when `v` is not `2` (Q12: no compatibility).

## Rules for values

- Numbers are numbers. Bytes and bytes per second are integers. Never a preformatted string.
- `progress` is 0..1. `eta` is seconds, `-1` for unknown.
- Times (`t`, `at`) are unix seconds.
- A ratio is a number; `-1` means "no ratio yet" (nothing downloaded).
- Paths are local paths, after the server's path map. `""` when a path cannot be mapped; the
  widget then disables "open folder" for that row.
- Lists are always present, possibly empty. Objects are always present.

## Schema

```jsonc
{
  "v": 2,
  "t": 1789346626,
  "pid": 48213,
  "app": {
    "version": "0.1.0",
    "window": "shown"                 // "shown" | "hidden"
  },
  "server": {
    "name": "home",
    "url": "127.0.0.1:8080",          // host:port only, never a credential
    "version": "5.2.3",               // qBittorrent's version, "" until known
    "reachable": true,                // false while the app cannot reach the server
    "error": "",                      // a short phrase while unreachable: "connection refused"
    "retryIn": 0                      // seconds until the next retry while unreachable
  },
  "reachable": "connected",           // server_state.connection_status:
                                      //   "connected" | "firewalled" | "disconnected"
  "speed":  { "dl": 4415221, "up": 831488 },
  "alt": {
    "on": false,
    "dlLimit": 0, "upLimit": 0        // the limits in force while on (bytes/s, 0 = none)
  },
  "counts": { "all": 142, "downloading": 6, "seeding": 118, "completed": 121,
              "stopped": 12, "stalled": 3, "checking": 1, "errored": 2 },
  "disk":   { "free": 1978765344768 },

  // I1, I5: what you gave. Session = since omaseed connected to this server.
  "share": {
    "sessionUp": 2254857830, "sessionDown": 1181116006, "sessionRatio": 1.91,
    "allTimeUp": 9876543210987, "allTimeDown": 4567890123456, "allTimeRatio": 2.16,
    "onOffer": 1374389534720          // total size of the complete torrents
  },

  // I2: average upload per minute over the last hour, oldest first, 60 numbers (bytes/s).
  // Minutes before omaseed started are 0.
  "uploadHour": [0, 0, 1200, 5400],

  // I5: upload per calendar day (local time) for the last 7 days, oldest first, today last.
  "week": [ { "day": "2026-09-26", "up": 3221225472 } ],

  // I3: complete, seeding torrents grouped by seeds in the swarm (num_complete, you included,
  // at least 1). One bucket per seed count from 1 to 5; the widget adds up the buckets up to
  // its `rareSeeds` option.
  //   count    torrents in the bucket
  //   waiting  of those, the ones with peers waiting (num_incomplete > 0)
  //   peers    the peers waiting on them, added up (num_incomplete)
  //   quiet    of those, the ones nobody has touched in 30 days (last_activity)
  "alive": {
    "buckets": [ { "seeds": 1, "count": 4, "waiting": 1, "peers": 9,
                   "quiet": 2 } ],
    // up to 6 rows with 5 seeds or fewer, rarest first: seeds ascending, then waiting
    // descending. The widget keeps the ones under its threshold and shows three.
    "rows": [ { "hash": "…", "name": "…", "seeds": 1, "waiting": 3 } ]
  },

  // I4: today's top seeders, by upload this session (uploaded_session), at most 2, and
  // only torrents that uploaded something.
  "topSeeders": [ { "hash": "…", "name": "…", "uploaded": 1288490188, "ratio": 3.4 } ],

  // Up to 3 downloads, fastest first.
  "active": [ { "hash": "…", "name": "…", "progress": 0.489, "dl": 1929379, "eta": 160 } ],

  // Completions and ratio milestones (I6), newest first, at most 5. Kept in state.json so
  // the list survives a restart.
  //   kind "completed": size, category, path
  //   kind "milestone": ratio (1, 2 or 5), path
  "recent": [
    { "kind": "completed", "hash": "…", "name": "…", "at": 1789346021,
      "size": 1406000000, "category": "linux", "path": "/data/linux/arch.iso" },
    { "kind": "milestone", "hash": "…", "name": "…", "at": 1789340000,
      "ratio": 2, "path": "/data/…" }
  ],

  // Add: the server's categories, and what the add uses when no category is picked.
  "categories": [ { "name": "linux", "savePath": "/data/linux" } ],
  "add": {
    "savePath": "/data/torrents",     // the server's default save path, "" until known
    "autoTmm": false,                 // the server adds with automatic management
    "category": "linux"               // the category the last add used ("" for none)
  },

  // Add: today's .torrent files in ~/Downloads, newest first, at most 5.
  "downloads": [ { "path": "/home/me/Downloads/x.torrent", "name": "x", "size": 1406000000,
                   "at": 1789346021, "inLibrary": false } ]
}
```

While the server is unreachable the app keeps writing the last known lists, `speed` is
zeros, and `server.reachable` is false; the widget shows the lists at 45 % and read-only.

## Acting

The widget never writes this file. It acts through D-Bus
(`io.github.diogochaves.omaseed.Status`, contract §5) and sees the result in the next write.

Bus name `io.github.diogochaves.omaseed`, object `/io/github/diogochaves/omaseed`, interface
`io.github.diogochaves.omaseed.Status`. Methods only: no properties, no signals. Call them
fire-and-forget, for example:

```
busctl --user call io.github.diogochaves.omaseed /io/github/diogochaves/omaseed \
  io.github.diogochaves.omaseed.Status SetAltSpeed b true
```

| Method | Signature | Does |
|---|---|---|
| `ToggleAltSpeed` | `() → b` | flips alternative speed limits; returns the state asked for |
| `SetAltSpeed` | `(b on) → b` | sets them; returns `on` (the current state when there is no server) |
| `DescribeSources` | `(as sources) → s` | JSON, below: pasted or browsed sources, before they are listed |
| `AddSources` | `(as sources, s category, b stopped) → s` | adds them; JSON, below. Never raises the window |
| `OpenAddSheet` | `(as sources)` | raises omaseed on its own add sheet with these sources (`m`) |
| `ShowTorrent` | `(s hash)` | raises omaseed with the cursor on that torrent |
| `OpenFolder` | `(s hash)` | shows the torrent's content in the file manager |

A source is a magnet link, an `http(s)` URL, or a `.torrent` path (or `file://` URI).

`DescribeSources` answers a JSON array, one object per source, in order:

```jsonc
[ { "source": "magnet:?xt=…",          // as sent
    "kind": "magnet",                  // "magnet" | "url" | "file" | "none"
    "valid": true, "error": "",        // error: why it is not valid
    "name": "archlinux-2026.09.01-x86_64.iso",
    "size": 1406000000,                // bytes, 0 when the source does not say (a URL)
    "hash": "3f6c9a1b…",               // the id the server keys it by, "" for a URL
    "duplicate": false,                // the library already holds it
    "duplicateOn": "" } ]              // the server's name when it does ("already on home")
```

`AddSources` adds every valid source that is not a duplicate, with the category (its save
path and automatic management, as the app's add sheet does by default), stopped when asked.
When it sends anything, the category is remembered in state.json (section `widgetAdd`, key
`category`) for `add.category`. It answers:

```jsonc
{ "added":      [ { "source": "…", "name": "…", "hash": "…" } ],
  "duplicates": [ { "source": "…", "name": "…", "hash": "…" } ],  // already on the server
  "failed":     [ { "source": "…", "error": "not a magnet link, a URL or a .torrent file" } ] }
```

Launching and raising go through `org.freedesktop.Application` on the same object:
`Activate` raises omaseed (and starts it, being `DBusActivatable`). `ActivateAction` takes
two actions from the widget; any other name is ignored:

| Action | Does |
|---|---|
| `settings` | raises omaseed on its settings (the popup's gear) |
| `retry` | polls an unreachable server now, as the app's "retry now" does (`r`); no window |

```
busctl --user call io.github.diogochaves.omaseed /io/github/diogochaves/omaseed \
  org.freedesktop.Application ActivateAction 'sava{sv}' retry 0 0
```

`DescribeSources` answers one object per source in the order they were sent.

## The heartbeat

The widget touches `$XDG_RUNTIME_DIR/omaseed/widget.alive` every 30 s (creating it the first
time) and removes it when it unloads. While that file is under 90 s old, omaseed's tray icon
mode "auto" hides the tray icon (contract Q7); it is back within 90 s of the last touch.
