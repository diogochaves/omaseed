# omaseed: the library's answers

Every answer is one JSON object with "v": 1; fields are only added within it.
Times are UTC (ISO 8601), sizes and speeds plain integers.

## Exit codes

  0  answered (status answers even when the server doesn't)
  1  unreadable: that skin doesn't read; it is drawn as Omarchy's
  2  usage, unknown-server, ambiguous: fix the command; omaseed help <word>
     unknown-skin, bad-name, skin-exists, no-theme: the same, for skins
  3  not-running, too-old, no-bus, no-reply: omaseed can't be asked now
  4  not-current, unreachable, no-log, unknown-torrent, first-sync
  5  not-written: a file omaseed would write can't be, or would replace
     one that doesn't parse; nothing was written

Exit 3: omaseed isn't running. Say so; don't start it unless asked.
Exit 4: that server can't answer now (the JSON says why).
A failure is {"error": {"code", "message"}}; the message is for the person.

## What the answers hold

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
