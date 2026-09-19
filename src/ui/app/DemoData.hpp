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

#include <QJsonObject>
#include <QStringList>

namespace omaseed::app::DemoData {

/// The 16 names of spec §1 Fig. 1, in the order the figure draws them.
QStringList specNames();

/// A full `sync/maindata` update with `rows` torrents (clamped to at least 1), the spec's
/// 16 first. Carries `torrents`, `categories`, `tags`, `trackers`, `server_state` and
/// `full_update: true`, which is all SyncEngine::applyDemoData() reads.
QJsonObject maindata(int rows = 16);

/// The hero's numbers of spec §1 Fig. 1 as a `server_state` object: 4.21 MB/s down,
/// 812 KB/s up, 1.8 TB free, connected.
QJsonObject serverState();

} // namespace omaseed::app::DemoData
