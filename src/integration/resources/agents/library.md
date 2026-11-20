## Your library

omaseed is the person's torrent client. Ask the running omaseed, never the
torrent server: don't call qBittorrent's, Transmission's or Deluge's API,
don't read their files or omaseed's (state.json, servers.json, moves/), and
don't call the bar widget's D-Bus interface.

  omaseed status --json     the server, speeds, how many need the person
  omaseed errors --json     each torrent that needs them: why, and its fix
  omaseed log --json        qBittorrent's warnings, the last 50 (--since ID)
  omaseed moves --json      the move running, the last ones that landed

What you read holds the person's torrent names and folders, and goes to your
model's provider. Read what the question needs.

Exit 3: omaseed isn't running. Say so; don't start it unless asked.
Exit 4: that server can't answer now (the JSON says why).

### Exit codes

  0  answered (status answers even when the server doesn't)
  1  unreadable: that skin doesn't read; it is drawn as Omarchy's
  2  usage, unknown-server, ambiguous: fix the command; omaseed help <word>
     unknown-skin, bad-name, skin-exists, no-theme: the same, for skins
  3  not-running, too-old, no-bus, no-reply: omaseed can't be asked now
  4  not-current, unreachable, no-log, unknown-torrent, first-sync
  5  not-written: a file omaseed would write can't be, or would replace
     one that doesn't parse; nothing was written

A failure is {"error": {"code", "message"}}; the message is for the person.

### What the answers hold

- status: server {name, engine, address, reachable, connection, problem},
  speed {down, up} in bytes/s, questions {all, needsYou, arriving,
  landedToday, givingBack, resting}, moves, paused, and servers[] as last seen.
- errors: torrents[] with kind, cause, fact, sentence, since, state, savePath,
  fix {words, do, needs} and up to 3 log lines. Tell the person fix.words:
  this omaseed runs no fix for you. errors and log read only the server
  omaseed is on; --server another one is exit 4.
- log: qBittorrent only (no-log on the others). lines[] oldest first, each
  {id, at, level, message, torrent}; ask again with --since <next> for what
  came after (restarted: qBittorrent started again, these are from its
  first line). fromStart is true when qBittorrent's memory reaches back to its
  start, false when it doesn't, null for a warnings-or-worse read, which
  can't tell; oldest is when the first line that read kept was logged.
- moves: running[] (state, pausedFor, headline, line, torrents {of, there,
  failed, leftOut}, bytes) and landed[], each with back: what a move back
  would do (goBack, movedAgain, movedOutside, gone), null for a server
  omaseed isn't on. Any server's, with --server.

Times are UTC (ISO 8601), sizes and speeds plain integers.
Every answer is one JSON object with "v": 1; fields are only added within it.
