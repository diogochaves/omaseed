# omaseed: which files are whose

In ~/.config/omaseed/:
- AGENTS.md, CLAUDE.md and skin.schema.json are omaseed's guide, rewritten
  on every start.
- skins/ is the person's: the first skin clone makes it, and each skin is a
  folder in it.
- state.json, servers.json, moves/ and secrets/ are omaseed's: never open
  or edit them.

Passwords live in the keyring, or in secrets/ on a machine without one:
never read that folder.

check writes its pictures in ~/.cache/omaseed/check/, and nothing else.

The installed skill, /usr/share/omaseed/agents/skill/, is root's: never
edit it. An agent's skills folder links to it, so it follows each upgrade.
A copy a person made of it is theirs, and stays as they left it.
