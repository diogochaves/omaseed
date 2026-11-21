---
name: omaseed
description: "omaseed is the person's torrent client. Ask the running omaseed, never the torrent server. Use when they ask about their torrents or downloads (why one didn't finish, is stuck, slow, missing or in error, where a move went, what is seeding) or want omaseed to look different (its skin: warmer, cooler, rounder, sharper, denser, another font). Triggers: omaseed, torrent, download, magnet, seeding, qBittorrent, Transmission, Deluge, didn't finish, stuck, slow, omaseed's skin, omaseed's look, warmer, rounder."
allowed-tools: "Bash(omaseed status *) Bash(omaseed errors *) Bash(omaseed log *) Bash(omaseed moves *) Bash(omaseed check *) Bash(omaseed skin list *)"
---

# omaseed: ask the running app

omaseed is the person's torrent client; it answers as JSON.
"ref: FILE" is a page in ref/ beside this file: read it when the task needs it.

## Never
- The torrent server: no qBittorrent, Transmission or Deluge API or files.
- omaseed's own files: state.json, servers.json, moves/, secrets/.
- The bar widget's D-Bus interface, or busctl/gdbus on omaseed's bus.
- A way round do: exit 5 is the person's no (their setting or the window).
  Say what it said; never act some other way, never move their files.
- omaseed do unless they asked you to act. Asked only why: tell them
  fix.words.
- Start omaseed unless asked. Exit 3 means it isn't running: say so.
What you read holds their torrent names and folders, and goes to your
model's provider: read what the question needs.

## Their library
  omaseed status --json     the server, speeds, how many need the person
  omaseed errors --json     each torrent that needs them: why, and its fix
  omaseed moves --json      the move running, the last ones that landed
  omaseed log --json        qBittorrent's warnings, the last 50
Didn't finish, stuck, slow or missing: errors first, then moves.
status's questions.needsYou above 0: errors has the answer.

## Acting, when they ask you to
  omaseed do --list --json              what they let agents do now
  omaseed do ACTION HASH... --dry-run --json   every check, nothing done
  omaseed do ACTION HASH... --json      it may ask them in omaseed's window
  omaseed do --plan FILE --json         many torrents, one ask
Missing files: errors, find the files yourself, a plan, --dry-run, then
the plan: one ask. do never deletes files, renames, adds or sets anything.
Every field, action, exit code and the plan: ref: library.md

## Their skin
  omaseed skin list --json          the drawn skin, and theirs
  omaseed check NAME --json         reads, drops, lifts; a light and a dark picture
  omaseed skin clone DRAWN NAME     a copy in ~/.config/omaseed/skins/NAME/
  edit skins/NAME/skin.json; check NAME after each save, look at both pictures
  omaseed skin use NAME             draw it in the open window; ask them to look
The way back: omaseed skin use omarchy.

Where a wish lands (grep ~/.config/omaseed/skin.schema.json for a token;
never read it whole):
  warmer, cooler    grounds.*.tint, roles.accent, line.*Role
  rounder, sharper  radius, cut
  denser, airier    density
  a font            font.label, font.value
Theme roles and alphas, never a hex. lifted is omaseed keeping text readable.
The rules, every token family, sharing, omabox: ref: skins.md
Which files are whose: ref: files.md
