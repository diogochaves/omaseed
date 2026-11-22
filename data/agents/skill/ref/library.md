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
  5  not done (do): not-allowed (their setting), never, refused,
     no-answer (the window, 2 minutes), not-removable, not-found
     not-written: a file omaseed would write can't be, or would replace
     one that doesn't parse; nothing was written
  6  failed: the server refused some of a do; done[] holds the rest

Exit 3: omaseed isn't running. Say so; don't start it unless asked.
Exit 4: that server can't answer now (the JSON says why).
A failure is {"error": {"code", "message"}}; the message is for the person.

## What the answers hold

- status: server {name, engine, address, reachable, connection, problem},
  speed {down, up} in bytes/s, questions {all, needsYou, arriving,
  landedToday, givingBack, resting}, moves, paused, and servers[] as last seen.
- errors: torrents[] with kind, cause, fact, sentence, since, state, savePath,
  fix {words, do, needs} and up to 3 log lines. fix.do is the do action
  for it: locate for missing files; recheck once a drive is plugged in.
  errors and log read only the server omaseed is on; --server another
  one is exit 4.
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

## do: acting

The person sets it in Settings › General › Agents; it binds only
omaseed's words (omaseed help agents: rules for your own settings).
do --list --json: lines[] {kind, level, actions}.
  fixes   retry, start-here, recheck, reannounce, dismiss, locate;
          pause, resume (the whole server, no hashes)   off, ask, allow
  moves   move HASH... --to FOLDER, move-back MOVE      off, ask
  remove  remove HASH...                                off, ask
ask: omaseed's window asks the person, for up to 2 minutes.
HASH: errors' hash, or 8+ of its first characters. MOVE: moves' id.
Never through do: delete, rename, categories, tags, adds, settings,
servers, quit. No flag deletes a file (--delete-files is usage).

- locate HASH... --to FOLDER: for missing files. FOLDER holds the
  torrent's own folder or file (its name); nothing moves, then a
  recheck. Every wanted file must be there at its size, else not-found
  with found, files and the first missing.
- remove HASH...: the torrent off the list, never its files; only when
  the server says its files are gone and none is on disk, else
  not-removable. Kept 30 days; the person adds it back in the window.
- --dry-run: every check, nothing asked or done; would: run or ask,
  and the real run's exit.

The answer: action, asked, answer (allowed, refused, no-answer,
not-asked), done[], failed[] {hash, name, code, message}, outcome; a
move adds job, locate found and files, remove kept (the day it goes).

## A plan: one ask for many

  omaseed do --plan FILE|- [--dry-run] --json
  {"lines": [{"action": "locate", "torrents": ["71ad02bc"], "to": "/x"},
             {"action": "remove", "torrents": ["9c40e7a2"]}]}
Every line is checked first: one off or refused line runs nothing
(lines[].would; its torrents[], each with why). Else one window asks for the lot,
each torrent ticked; the person may untick some. The answer: lines[]
{action, done, failed, unticked, outcome}.

## Missing files, sorted out

1. errors --json: kind missing-files; savePath. A drive not mounted: the
   person plugs it in, then recheck.
2. Look for each on disk with your shell: its name, then its size.
3. A plan: locate where you found it, remove only what is nowhere.
4. do --plan --dry-run, fix what it refuses (has files: locate or
   recheck), and tell the person what it will do.
5. The plan, once: one window asks. Then errors again.
