#pragma once

// DemoData — fictional torrents shaped exactly like a `sync/maindata` full update, so the
// views can be drawn (and screenshotted) with no server anywhere near them.
//
// The first 16 torrents are the 16 rows of spec §1 Fig. 1: their names, categories, sizes,
// progress, status, S / P, speeds and ETA are the figure's own values, so a screenshot of
// `--demo table` can be laid next to the figure column by column. Everything the figure does
// not show (hashes, save paths, timestamps, ratios, announce URLs) is generated plausibly.
// Rows past 16 are generated from a seeded QRandomGenerator, so the same `rows` always gives
// the same library — a screenshot taken twice is the same screenshot.
//
// Nothing here names a real tracker or a real host: the announce URLs live under
// `example.org` / `example.net`, which RFC 2606 reserves.

#include "core/Torrent.hpp"

#include <QJsonObject>
#include <QStringList>

namespace omaseed::app::DemoData {

/// The 16 names of spec §1 Fig. 1, in the order the figure draws them.
QStringList specNames();

/// A full `sync/maindata` update with `rows` torrents (clamped to at least 1), the spec's
/// 16 first. Carries `torrents`, `categories`, `tags`, `trackers`, `server_state` and
/// `full_update: true`, which is all SyncEngine::applyDemoData() reads.
///
/// `OMASEED_DEMO_GROUPS=<1..6>` multiplies the rail's category, tag and tracker groups (4/3/2
/// at 1, the default, which is the library the figures draw; 12/9/6 at 3), to see the rail
/// under a crowded library. The spec's 16 rows keep their own category, tags and tracker at
/// every setting, and at 1 the generated rows are byte-identical to what they always were,
/// so a screenshot meant for the figures is unaffected.
QJsonObject maindata(int rows = 16);

/// The hero's numbers of spec §1 Fig. 1 as a `server_state` object: 4.21 MB/s down,
/// 812 KB/s up, 1.8 TB free, connected.
QJsonObject serverState();

/// Everything the details pane (spec §2) shows for one torrent of the demo library, shaped
/// for TorrentDetails::applyDemo(): `{hash, properties, files, trackers, peers}`, where each
/// value has the shape of the API call it stands in for (`torrents/properties`,
/// `torrents/files`, `torrents/trackers`, `sync/torrentPeers`).
///
/// Two rows are the spec's own figures: `Big Buck Bunny 4K (CC-BY)` carries Fig. 2's file
/// tree, and `archlinux-2026.09.01-x86_64.iso` Fig. 3's trackers and Fig. 4's peers. Every
/// other row gets plausible data generated from its own fields, deterministically, so a
/// screenshot taken twice is the same screenshot.
QJsonObject details(const omaseed::core::Torrent &torrent);

} // namespace omaseed::app::DemoData
