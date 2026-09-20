# Changelog

All notable changes to omaseed are logged here, newest first. Loosely follows
[Keep a Changelog](https://keepachangelog.com/); omaseed does not yet promise semantic
versioning stability (pre-1.0). `scripts/release.sh` requires a `## v<version>` section here
before it will tag a release.

## Unreleased

Nothing yet.

## v0.1.0 — 2026-09-19

First release. A native Qt 6 Quick desktop client for one or many qBittorrent servers,
built for Omarchy.

### Added

- Live Omarchy theme following across all bundled themes and base sizes, with a D-Bus
  `Reload` call fired by the `theme-set` hook for instant repaint on `omarchy-theme-set`.
- System tray presence, single-instance activation, and `magnet:` / `.torrent` file
  hand-off to an already-running instance.
- Desktop notifications for completed downloads, lost/restored server connections, and
  low disk space, each individually toggleable; the low-disk line is a setting (default
  2 GiB) that watches the server's free space for its default save path.
- A virtualised torrent table, a rail of status/category/tag/server filters, search, and
  a details pane (overview, files, trackers, peers).
- Command palettes: jump-to-torrent, category, tags, sort, and column visibility, plus a
  keyboard help sheet covering every shortcut.
- Adding torrents from a magnet link, a `.torrent` file, the clipboard, drag-and-drop, or
  the portal file picker, with duplicate detection before the add lands.
- Per-torrent actions: start/stop, force start, recheck, reannounce, category, tags,
  location, share limits, rename, queue position, delete (with or without files), and
  copy-to-clipboard.
- Multi-server support: a settings modal and server sheet, credentials in `libsecret`
  (0600 file fallback), default-server selection, and live connection testing.
- Empty, filtered-empty, disconnected, and loading states, and a footer of live facts.

### Known gaps (tracked for the v1 exit in `docs/06-implementation-plan.md`)

- The first-run detection/setup wizard and the Phase 4 integration pass were still in
  progress when this entry was written; see `docs/PROGRESS.md`.
- Packaging (`packaging/arch/PKGBUILD`, `packaging/omarchy/package.json`) and the
  user-facing README are being built alongside this release script (Phase 5).
